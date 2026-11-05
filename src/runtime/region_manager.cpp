/// @file region_manager.cpp
/// @brief OS-level memory region management (mmap on Linux/macOS, VirtualAlloc on Windows).

#include "region_manager.hpp"
#include <fragmentx/statistics.hpp>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <new>

#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
#  include <sys/mman.h>
#  include <unistd.h>
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace fragmentx {
namespace runtime {

// ─── Platform helpers ────────────────────────────────────────────────────────

static std::size_t page_size_impl() noexcept {
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
  SYSTEM_INFO si;
  ::GetSystemInfo(&si);
  return static_cast<std::size_t>(si.dwPageSize);
#else
  return 4096;
#endif
}

std::size_t RegionManager::page_size() noexcept {
  static const std::size_t ps = page_size_impl();
  return ps;
}

RegionManager::~RegionManager() {
  std::vector<Region*> remaining;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    remaining.swap(regions_);
  }

  for (Region* region : remaining) {
    if (!region) continue;
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
    ::munmap(region->base, region->size);
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
    ::VirtualFree(region->base, 0, MEM_RELEASE);
#else
    std::free(region->base);
#endif
    delete region;
  }
}

static std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
  return (value + alignment - 1) & ~(alignment - 1);
}

// ─── Region allocation ───────────────────────────────────────────────────────

Region* RegionManager::acquire(std::size_t min_size, RegionType type) {
  const std::size_t ps   = page_size();
  const std::size_t size = align_up(min_size, ps);

#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  void* base = ::mmap(
      nullptr, size,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1, 0);
  if (base == MAP_FAILED) {
    return nullptr;
  }
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
  void* base = ::VirtualAlloc(nullptr, size,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!base) return nullptr;
#else
  // Fallback: system malloc (limited functionality)
  void* base = std::malloc(size);
  if (!base) return nullptr;
#endif

  std::lock_guard<std::mutex> lock(mutex_);
  auto* region          = new Region{};
  region->base          = static_cast<std::byte*>(base);
  region->size          = size;
  region->committed     = size;
  region->cursor        = 0;
  region->type          = type;
  region->in_use        = true;

  stats_.total_reserved  += size;
  stats_.total_committed += size;
  stats_.regions_acquired++;

  regions_.push_back(region);
  return region;
}

void RegionManager::release(Region* region) {
  if (!region) return;

  std::lock_guard<std::mutex> lock(mutex_);

#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  ::munmap(region->base, region->size);
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
  ::VirtualFree(region->base, 0, MEM_RELEASE);
#else
  std::free(region->base);
#endif

  stats_.total_released += region->size;
  stats_.regions_released++;
  region->in_use = false;

  // Remove from active list.
  auto it = std::find(regions_.begin(), regions_.end(), region);
  if (it != regions_.end()) regions_.erase(it);

  delete region;
}

void RegionManager::decommit_unused(Region* region) {
  if (!region) return;
#if defined(FRAGMENTX_PLATFORM_LINUX)
  // Advise kernel that pages beyond cursor are no longer needed.
  const std::size_t ps = page_size();
  const std::size_t used_pages = align_up(region->cursor, ps);
  if (used_pages < region->size) {
    std::byte* start = region->base + used_pages;
    std::size_t len  = region->size - used_pages;
    ::madvise(start, len, MADV_DONTNEED);
    region->committed = used_pages;
  }
#elif defined(FRAGMENTX_PLATFORM_MACOS)
  const std::size_t ps = page_size();
  const std::size_t used_pages = align_up(region->cursor, ps);
  if (used_pages < region->size) {
    std::byte* start = region->base + used_pages;
    ::madvise(start, region->size - used_pages, MADV_FREE);
    region->committed = used_pages;
  }
#endif
  // Windows: VirtualFree with MEM_DECOMMIT for similar effect.
}

RegionStats RegionManager::statistics() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

} // namespace runtime
} // namespace fragmentx

