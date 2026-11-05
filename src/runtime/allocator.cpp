/// @file allocator.cpp
/// @brief fragmentx::Allocator — the top-level allocator tying all components together.

#include <fragmentx/allocator.hpp>
#include <fragmentx/policy.hpp>
#include <fragmentx/capture.hpp>
#include "region_manager.hpp"
#include "arena_manager.hpp"
#include "allocation_router.hpp"
#include "metadata_store.hpp"
#include "thread_cache.hpp"
#include "size_classes.hpp"

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <atomic>
#include <thread>
#if defined(_MSC_VER)
#  include <intrin.h>
#endif

#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
#  include <sys/mman.h>
#endif

// AddressSanitizer annotations (no-ops when not building with ASan).
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define FRAGMENTX_HAVE_ASAN 1
#    include <sanitizer/asan_interface.h>
#    define FX_POISON(ptr, sz)   ASAN_POISON_MEMORY_REGION(ptr, sz)
#    define FX_UNPOISON(ptr, sz) ASAN_UNPOISON_MEMORY_REGION(ptr, sz)
#  endif
#endif
#ifndef FX_POISON
#  define FX_POISON(ptr, sz)   (void)(ptr); (void)(sz)
#  define FX_UNPOISON(ptr, sz) (void)(ptr); (void)(sz)
#endif

namespace fragmentx {

// ─── Timestamp helper ────────────────────────────────────────────────────────

static uint64_t now_ns() noexcept {
#if defined(FRAGMENTX_PLATFORM_LINUX)
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
       + static_cast<uint64_t>(ts.tv_nsec);
#elif defined(FRAGMENTX_PLATFORM_MACOS)
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
       + static_cast<uint64_t>(ts.tv_nsec);
#else
  auto tp = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(tp).count());
#endif
}

// ─── Impl ────────────────────────────────────────────────────────────────────

struct Allocator::Impl {
  Config                       config;
  runtime::RegionManager       regions;
  runtime::ArenaManager        arenas;
  runtime::AllocationRouter    router;
  runtime::MetadataStore       metadata;

  // Arena IDs (created during init).
  uint32_t default_arena_id   = 0;
  uint32_t longlived_arena_id = 0;
  uint32_t fallback_arena_id  = 0;  // Not a real FragmentX arena; marker only.

  // Per-thread cache (thread_local means one instance per thread per Impl).
  // We use a pointer to allow cleanup in destructors.
  static thread_local runtime::ThreadCache* tl_cache;

  // Statistics.
  std::atomic<uint64_t> stat_allocs{0};
  std::atomic<uint64_t> stat_frees{0};
  std::atomic<uint64_t> stat_reallocs{0};
  std::atomic<uint64_t> stat_requested_bytes{0};
  std::atomic<uint64_t> stat_fallback_allocs{0};
  std::atomic<uint64_t> stat_cache_hits{0};
  std::atomic<uint64_t> stat_cache_misses{0};

  std::string active_policy_name;

  explicit Impl(const Config& cfg)
      : config(cfg)
      , arenas(regions, cfg.region_size)
  {
    // Create the default arenas.
    ArenaConfig gen_cfg;
    gen_cfg.type         = ArenaType::GeneralPurpose;
    gen_cfg.initial_size = cfg.region_size;
    gen_cfg.label        = "default";
    default_arena_id     = arenas.create_arena(gen_cfg);

    ArenaConfig ll_cfg;
    ll_cfg.type          = ArenaType::LongLived;
    ll_cfg.initial_size  = cfg.region_size / 2;
    ll_cfg.label         = "longlived";
    longlived_arena_id   = arenas.create_arena(ll_cfg);

    router.set_default_arena(default_arena_id);
    router.set_longlived_arena(longlived_arena_id);

    // Load policy if specified.
    if (!cfg.policy_path.empty()) {
      auto opt = PolicyLoader::load(cfg.policy_path);
      if (opt) {
        router.load_policy(*opt);
        active_policy_name = opt->name;
      }
    }
  }

  runtime::ThreadCache* get_thread_cache() {
    if (!tl_cache) {
      tl_cache = new runtime::ThreadCache{};
    }
    return tl_cache;
  }

  [[nodiscard]] void* do_allocate(std::size_t size, std::size_t alignment,
                                   LifetimeHint hint) noexcept
  {
    if (size == 0) size = 1; // Guarantee unique non-null pointer.

#if defined(_MSC_VER)
    const void* return_address = _ReturnAddress();
#elif defined(__GNUC__) || defined(__clang__)
    const void* return_address = __builtin_return_address(0);
#else
    const void* return_address = nullptr;
#endif
    const uint64_t site_hash = site_hash_from_address(return_address);

    auto decision = router.route(size, alignment, site_hash, hint);

    // Try thread-local cache first (small allocations only).
    if (!decision.use_fallback
        && decision.size_class_idx < runtime::NUM_SIZE_CLASSES)
    {
      auto* cache = get_thread_cache();
      void* cached = cache->per_class[decision.size_class_idx].pop();
      if (cached) {
        FX_UNPOISON(cached, decision.rounded_size);
        stat_cache_hits.fetch_add(1, std::memory_order_relaxed);
        stat_allocs.fetch_add(1, std::memory_order_relaxed);
        stat_requested_bytes.fetch_add(size, std::memory_order_relaxed);
        // Store metadata.
        runtime::AllocationRecord rec{};
        rec.ptr              = cached;
        rec.requested_size   = size;
        rec.allocated_size   = decision.rounded_size;
        rec.arena_id         = decision.arena_id;
        rec.size_class_index = decision.size_class_idx;
        rec.alignment        = alignment ? alignment : config.default_alignment;
        rec.alloc_time_ns    = now_ns();
        metadata.insert(cached, rec);
        return cached;
      }
      stat_cache_misses.fetch_add(1, std::memory_order_relaxed);
    }

    // Fall back to system allocator for very large requests or when flagged.
    if (decision.use_fallback || config.fallback_to_system) {
      if (decision.use_fallback) {
        const std::size_t eff_align = alignment ? alignment : config.default_alignment;
        void* ptr = nullptr;
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
        if (::posix_memalign(&ptr, eff_align, size) != 0) ptr = nullptr;
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
        ptr = ::_aligned_malloc(size, eff_align);
#else
        ptr = std::aligned_alloc(eff_align, (size + eff_align - 1) & ~(eff_align - 1));
#endif
        if (ptr) {
          stat_allocs.fetch_add(1, std::memory_order_relaxed);
          stat_fallback_allocs.fetch_add(1, std::memory_order_relaxed);
          stat_requested_bytes.fetch_add(size, std::memory_order_relaxed);
          runtime::AllocationRecord rec{};
          rec.ptr = ptr; rec.requested_size = size; rec.allocated_size = size;
          rec.arena_id = UINT32_MAX; // Sentinel: system-allocated.
          metadata.insert(ptr, rec);
        }
        return ptr;
      }
    }

    // Serve from arena.
    runtime::Arena* arena = arenas.get_arena(decision.arena_id);
    if (!arena) arena = arenas.get_arena(default_arena_id);
    if (!arena) return nullptr;

    const std::size_t eff_align = alignment ? alignment : config.default_alignment;
    void* ptr = arena->allocate(decision.rounded_size, eff_align);

    if (!ptr && config.fallback_to_system) {
      // Arena exhausted; use system malloc.
      ptr = std::malloc(size);
      if (!ptr) return nullptr;
      stat_fallback_allocs.fetch_add(1, std::memory_order_relaxed);
    }

    if (ptr) {
      FX_UNPOISON(ptr, decision.rounded_size);
      stat_allocs.fetch_add(1, std::memory_order_relaxed);
      stat_requested_bytes.fetch_add(size, std::memory_order_relaxed);
      runtime::AllocationRecord rec{};
      rec.ptr              = ptr;
      rec.requested_size   = size;
      rec.allocated_size   = decision.rounded_size;
      rec.arena_id         = decision.arena_id;
      rec.size_class_index = decision.size_class_idx;
      rec.alignment        = eff_align;
      rec.alloc_time_ns    = now_ns();
      metadata.insert(ptr, rec);
    }
    return ptr;
  }
};

thread_local runtime::ThreadCache* Allocator::Impl::tl_cache = nullptr;

// ─── Allocator public API ────────────────────────────────────────────────────

Allocator::Allocator(const Config& config)
    : impl_(std::make_unique<Impl>(config))
{}

Allocator::~Allocator() = default;
Allocator::Allocator(Allocator&&) noexcept = default;
Allocator& Allocator::operator=(Allocator&&) noexcept = default;

void* Allocator::allocate(std::size_t size, std::size_t alignment) noexcept {
  return impl_->do_allocate(size, alignment, LifetimeHint::Unknown);
}

void* Allocator::allocate_with_hint(std::size_t size, std::size_t alignment,
                                     LifetimeHint hint) noexcept {
  return impl_->do_allocate(size, alignment, hint);
}

void Allocator::deallocate(void* ptr) noexcept {
  if (!ptr) return;

  const runtime::AllocationRecord* rec = impl_->metadata.lookup(ptr);
  if (!rec) {
    // Unknown pointer; pass to system free as safety net.
    std::free(ptr);
    return;
  }

  const uint32_t arena_id    = rec->arena_id;
  const uint32_t class_idx   = rec->size_class_index;
  const std::size_t alloc_sz = rec->allocated_size;
  impl_->metadata.remove(ptr);

  // System-allocated fallback: free directly.
  if (arena_id == UINT32_MAX) {
#if defined(FRAGMENTX_PLATFORM_WINDOWS)
    ::_aligned_free(ptr);
#else
    std::free(ptr);
#endif
    impl_->stat_frees.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // Try thread-local cache first.
  if (class_idx < runtime::NUM_SIZE_CLASSES) {
    auto* cache = impl_->get_thread_cache();
    auto& pc = cache->per_class[class_idx];
    if (!pc.full()) {
      FX_POISON(ptr, alloc_sz);
      pc.push(ptr);
      impl_->stat_frees.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    // Cache full: flush half to global arena, then push current block.
    runtime::Arena* arena = impl_->arenas.get_arena(arena_id);
    if (arena) {
      for (std::size_t i = 0; i < pc.count / 2; ++i) {
        arena->deallocate(pc.slots[i], class_idx);
      }
      std::size_t remaining = pc.count - pc.count / 2;
      std::memmove(pc.slots.data(), pc.slots.data() + pc.count / 2,
                   remaining * sizeof(void*));
      pc.count = remaining;
    }
    FX_POISON(ptr, alloc_sz);
    pc.push(ptr);
  } else {
    FX_POISON(ptr, alloc_sz);
    runtime::Arena* arena = impl_->arenas.get_arena(arena_id);
    if (arena) arena->deallocate(ptr, class_idx);
    else std::free(ptr);
  }
  impl_->stat_frees.fetch_add(1, std::memory_order_relaxed);
}

void* Allocator::reallocate(void* ptr, std::size_t new_size) noexcept {
  if (!ptr)     return allocate(new_size);
  if (!new_size) { deallocate(ptr); return nullptr; }

  const runtime::AllocationRecord* rec = impl_->metadata.lookup(ptr);
  if (rec && rec->allocated_size >= new_size) {
    // In-place resize: enough room in the existing block.
    // Update metadata.
    runtime::AllocationRecord updated = *rec;
    updated.requested_size = new_size;
    impl_->metadata.insert(ptr, updated);
    impl_->stat_reallocs.fetch_add(1, std::memory_order_relaxed);
    return ptr;
  }

  // Allocate new block + copy.
  const std::size_t old_size = rec ? rec->requested_size : new_size;
  void* new_ptr = allocate(new_size);
  if (!new_ptr) return nullptr;
  std::memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
  deallocate(ptr);
  impl_->stat_reallocs.fetch_add(1, std::memory_order_relaxed);
  return new_ptr;
}

bool Allocator::load_policy(const std::string& path) {
  auto opt = PolicyLoader::load(path);
  if (!opt) return false;
  impl_->router.load_policy(*opt);
  impl_->active_policy_name = opt->name;
  return true;
}

bool Allocator::apply_policy(const AllocationPolicy& policy) {
  impl_->router.load_policy(policy);
  impl_->active_policy_name = policy.name;
  return true;
}

void Allocator::clear_policy() noexcept {
  impl_->router.clear_policy();
  impl_->active_policy_name.clear();
}

bool Allocator::has_policy() const noexcept {
  return impl_->router.has_policy();
}

std::string Allocator::active_policy_name() const noexcept {
  return impl_->active_policy_name;
}

AllocatorStatistics Allocator::statistics() const noexcept {
  AllocatorStatistics s;
  s.total_allocations   = impl_->stat_allocs.load(std::memory_order_relaxed);
  s.total_deallocations = impl_->stat_frees.load(std::memory_order_relaxed);
  s.total_reallocations = impl_->stat_reallocs.load(std::memory_order_relaxed);
  s.cache_hits          = impl_->stat_cache_hits.load(std::memory_order_relaxed);
  s.cache_misses        = impl_->stat_cache_misses.load(std::memory_order_relaxed);
  s.fallback_allocations= impl_->stat_fallback_allocs.load(std::memory_order_relaxed);
  s.requested_bytes     = impl_->stat_requested_bytes.load(std::memory_order_relaxed);

  auto rs = impl_->regions.statistics();
  s.region_bytes_total    = rs.total_reserved;
  s.region_bytes_committed= rs.total_committed;
  s.regions_acquired      = rs.regions_acquired;
  s.regions_released      = rs.regions_released;
  s.active_arenas         = impl_->arenas.count();

  if (s.requested_bytes > 0 && s.reserved_bytes > 0) {
    s.internal_fragmentation_ratio =
        static_cast<double>(s.reserved_bytes - s.requested_bytes)
      / static_cast<double>(s.reserved_bytes);
  }
  return s;
}

void Allocator::reset_statistics() noexcept {
  impl_->stat_allocs.store(0, std::memory_order_relaxed);
  impl_->stat_frees.store(0, std::memory_order_relaxed);
  impl_->stat_reallocs.store(0, std::memory_order_relaxed);
  impl_->stat_requested_bytes.store(0, std::memory_order_relaxed);
  impl_->stat_fallback_allocs.store(0, std::memory_order_relaxed);
  impl_->stat_cache_hits.store(0, std::memory_order_relaxed);
  impl_->stat_cache_misses.store(0, std::memory_order_relaxed);
}

void Allocator::flush_thread_caches() noexcept {
  // No global registry of threads; flushing is done lazily per-thread.
  // This function is a no-op in the current implementation.
}

} // namespace fragmentx

