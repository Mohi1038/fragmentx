/// @file interposition_macos.cpp
/// @brief macOS DYLD_INSERT_LIBRARIES allocation tracer.

#if !defined(__APPLE__)
#  error "This source is for macOS only"
#endif

#include <fragmentx/capture.hpp>
#include <fragmentx/trace.hpp>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

namespace {

thread_local bool in_allocator = false;
thread_local bool in_hook = false;
std::atomic<uint64_t> next_event{0};
std::atomic<bool> write_failed{false};
std::atomic<std::size_t> bootstrap_used{0};
alignas(std::max_align_t) unsigned char bootstrap[64 * 1024];
int trace_fd = -1;
pid_t session_owner_pid = 0;

bool is_bootstrap(const void* ptr) noexcept {
  const auto value = reinterpret_cast<uintptr_t>(ptr);
  const auto begin = reinterpret_cast<uintptr_t>(bootstrap);
  return value >= begin && value < begin + sizeof(bootstrap);
}

void* bootstrap_alloc(std::size_t size) noexcept {
  constexpr std::size_t alignment = alignof(std::max_align_t);
  constexpr std::size_t header = (sizeof(std::size_t) + alignment - 1) & ~(alignment - 1);
  if (size > SIZE_MAX - alignment - header) return nullptr;
  const std::size_t payload = (size + alignment - 1) & ~(alignment - 1);
  const std::size_t total = header + payload;
  std::size_t old = bootstrap_used.load(std::memory_order_relaxed);
  while (old <= sizeof(bootstrap) && total <= sizeof(bootstrap) - old) {
    if (bootstrap_used.compare_exchange_weak(old, old + total,
          std::memory_order_relaxed, std::memory_order_relaxed)) {
      auto* result = bootstrap + old + header;
      std::memcpy(result - sizeof(size), &size, sizeof(size));
      return result;
    }
  }
  return nullptr;
}

uint64_t timestamp_ns(clockid_t clock) noexcept {
  timespec ts{};
  if (::clock_gettime(clock, &ts) != 0) return 0;
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t thread_id() noexcept {
  uint64_t tid = 0;
  (void)::pthread_threadid_np(nullptr, &tid);
  return (static_cast<uint64_t>(static_cast<uint32_t>(::getpid())) << 32U) |
         (tid & 0xffffffffULL);
}

uint64_t hash_site(const void* address) noexcept {
  uint64_t hash = 14695981039346656037ULL;
  uintptr_t value = reinterpret_cast<uintptr_t>(address);
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    hash ^= static_cast<uint8_t>(value & 0xffU);
    hash *= 1099511628211ULL;
    value >>= 8U;
  }
  return hash;
}

bool write_all(const void* data, std::size_t size) noexcept {
  const auto* cursor = static_cast<const unsigned char*>(data);
  while (size != 0) {
    const ssize_t written = ::write(trace_fd, cursor, size);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return false;
    cursor += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

void write_event(const fragmentx::TraceRecordHeader& header, void* payload,
                 std::size_t payload_size, uint64_t& event_id) noexcept {
  event_id = (static_cast<uint64_t>(static_cast<uint32_t>(::getpid())) << 32U) |
             (next_event.fetch_add(1, std::memory_order_relaxed) & 0xffffffffULL);
  iovec records[2]{{const_cast<fragmentx::TraceRecordHeader*>(&header), sizeof(header)},
                   {payload, payload_size}};
  in_allocator = true;
  ssize_t written;
  do { written = ::writev(trace_fd, records, 2); }
  while (written < 0 && errno == EINTR);
  in_allocator = false;
  if (written != static_cast<ssize_t>(sizeof(header) + payload_size))
    write_failed.store(true, std::memory_order_relaxed);
}

void emit_alloc(void* ptr, std::size_t size, const void* site) noexcept {
  if (trace_fd < 0 || !ptr || in_hook || in_allocator) return;
  in_hook = true;
  fragmentx::TraceRecordHeader header{};
  header.type = static_cast<uint8_t>(fragmentx::TraceRecordType::Alloc);
  header.length = static_cast<uint16_t>(sizeof(fragmentx::AllocPayload));
  fragmentx::AllocPayload payload{};
  payload.address = reinterpret_cast<uint64_t>(ptr);
  payload.requested_size = size;
  payload.allocated_size = 0;
  payload.timestamp_ns = timestamp_ns(CLOCK_MONOTONIC_RAW);
  payload.site_hash = hash_site(site);
  payload.thread_id = thread_id();
  write_event(header, &payload, sizeof(payload), payload.event_id);
  in_hook = false;
}

void emit_free(void* ptr) noexcept {
  if (trace_fd < 0 || !ptr || in_hook || in_allocator) return;
  in_hook = true;
  fragmentx::TraceRecordHeader header{};
  header.type = static_cast<uint8_t>(fragmentx::TraceRecordType::Free);
  header.length = static_cast<uint16_t>(sizeof(fragmentx::FreePayload));
  fragmentx::FreePayload payload{};
  payload.address = reinterpret_cast<uint64_t>(ptr);
  payload.timestamp_ns = timestamp_ns(CLOCK_MONOTONIC_RAW);
  payload.thread_id = thread_id();
  write_event(header, &payload, sizeof(payload), payload.event_id);
  in_hook = false;
}

} // namespace

extern "C" __attribute__((constructor)) void fragmentx_capture_init() {
  const char* output = ::getenv("FRAGMENTX_TRACE_OUTPUT");
  if (!output || !*output) return;
  const char* mode = ::getenv("FRAGMENTX_TRACE_MODE");
  const bool append = mode && std::strcmp(mode, "append") == 0;
  trace_fd = ::open(output, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC |
                    (append ? 0 : O_TRUNC), 0644);
  if (trace_fd < 0) return;
  const char* owner = ::getenv("FRAGMENTX_TRACE_OWNER_PID");
  session_owner_pid = owner ? static_cast<pid_t>(std::strtol(owner, nullptr, 10)) : ::getpid();
  if (session_owner_pid <= 0) session_owner_pid = ::getpid();
  if (append) {
    const off_t end = ::lseek(trace_fd, 0, SEEK_END);
    if (end > 0) next_event.store(static_cast<uint64_t>(end), std::memory_order_relaxed);
    return;
  }

  fragmentx::TraceHeader header{};
  std::memcpy(header.magic, fragmentx::TRACE_MAGIC, sizeof(header.magic));
  header.format_version = fragmentx::TRACE_FORMAT_VERSION;
  header.pointer_size = static_cast<uint8_t>(sizeof(void*));
  header.platform = 1;
  header.reserved[0] = fragmentx::TRACE_HEADER_FLAG_PROCESS_SCOPED_ADDRESSES;
  header.timestamp_base_ns = timestamp_ns(CLOCK_MONOTONIC_RAW);
  header.wall_clock_epoch_ns = timestamp_ns(CLOCK_REALTIME);
  header.session_id = header.timestamp_base_ns ^
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&header));
  if (!write_all(&header, sizeof(header))) {
    write_failed.store(true, std::memory_order_relaxed);
    return;
  }
  char owner_buffer[32]{};
  const int owner_length = std::snprintf(owner_buffer, sizeof(owner_buffer), "%ld",
                                         static_cast<long>(session_owner_pid));
  if (owner_length <= 0 || static_cast<std::size_t>(owner_length) >= sizeof(owner_buffer) ||
      ::setenv("FRAGMENTX_TRACE_OWNER_PID", owner_buffer, 1) != 0 ||
      ::setenv("FRAGMENTX_TRACE_MODE", "append", 1) != 0)
    write_failed.store(true, std::memory_order_relaxed);
}

extern "C" __attribute__((destructor)) void fragmentx_capture_fini() {
  if (trace_fd < 0) return;
  if (::getpid() == session_owner_pid && !write_failed.load(std::memory_order_relaxed)) {
    fragmentx::TraceRecordHeader end{};
    end.type = static_cast<uint8_t>(fragmentx::TraceRecordType::EndOfTrace);
    (void)write_all(&end, sizeof(end));
  }
  ::close(trace_fd);
  trace_fd = -1;
}

extern "C" void* fragmentx_malloc(std::size_t size) {
  if (in_allocator) return bootstrap_alloc(size);
  in_allocator = true;
  malloc_zone_t* zone = ::malloc_default_zone();
  void* ptr = zone && zone->malloc ? zone->malloc(zone, size) : bootstrap_alloc(size);
  in_allocator = false;
  emit_alloc(ptr, size, __builtin_return_address(0));
  return ptr;
}
extern "C" void fragmentx_free(void* ptr) {
  if (!ptr || is_bootstrap(ptr) || in_allocator) return;
  emit_free(ptr);
  in_allocator = true;
  malloc_zone_t* zone = ::malloc_zone_from_ptr(ptr);
  if (zone && zone->free) zone->free(zone, ptr);
  in_allocator = false;
}
extern "C" void* fragmentx_calloc(std::size_t count, std::size_t size) {
  if (in_allocator) {
    if (count && size > SIZE_MAX / count) return nullptr;
    void* ptr = bootstrap_alloc(count * size);
    if (ptr) std::memset(ptr, 0, count * size);
    return ptr;
  }
  in_allocator = true;
  malloc_zone_t* zone = ::malloc_default_zone();
  void* ptr = zone && zone->calloc ? zone->calloc(zone, count, size) : nullptr;
  in_allocator = false;
  if (count == 0 || size <= SIZE_MAX / count)
    emit_alloc(ptr, count * size, __builtin_return_address(0));
  return ptr;
}
extern "C" void* fragmentx_realloc(void* ptr, std::size_t size) {
  if (is_bootstrap(ptr)) {
    void* replacement = fragmentx_malloc(size);
    if (replacement) {
      std::size_t old_size = 0;
      std::memcpy(&old_size, static_cast<unsigned char*>(ptr) - sizeof(old_size), sizeof(old_size));
      std::memcpy(replacement, ptr, old_size < size ? old_size : size);
    }
    return replacement;
  }
  in_allocator = true;
  malloc_zone_t* zone = ptr ? ::malloc_zone_from_ptr(ptr) : ::malloc_default_zone();
  void* replacement = zone && zone->realloc ? zone->realloc(zone, ptr, size) : nullptr;
  in_allocator = false;
  if (replacement) {
    if (ptr) emit_free(ptr);
    emit_alloc(replacement, size, __builtin_return_address(0));
  } else if (size == 0) {
    emit_free(ptr);
  }
  return replacement;
}
extern "C" void* fragmentx_aligned_alloc(std::size_t alignment, std::size_t size) {
  if (alignment == 0 || size % alignment != 0) { errno = EINVAL; return nullptr; }
  void* ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
  return ptr;
}
extern "C" int fragmentx_posix_memalign(void** ptr, std::size_t alignment,
                                         std::size_t size) {
  if (!ptr || alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0 ||
      alignment % sizeof(void*) != 0) return EINVAL;
  if (in_allocator) { *ptr = nullptr; return ENOMEM; }
  in_allocator = true;
  malloc_zone_t* zone = ::malloc_default_zone();
  void* result = zone && zone->memalign ? zone->memalign(zone, alignment, size) : nullptr;
  in_allocator = false;
  if (!result) return ENOMEM;
  *ptr = result;
  emit_alloc(result, size, __builtin_return_address(0));
  return 0;
}

#define FRAGMENTX_DYLD_INTERPOSE(replacement, replacee) \
  __attribute__((used)) static struct { const void* replacement; const void* replacee; } \
  interpose_##replacee __attribute__((section("__DATA,__interpose"))) = { \
    reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(&replacement)), \
    reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(&replacee)) }

FRAGMENTX_DYLD_INTERPOSE(fragmentx_malloc, malloc);
FRAGMENTX_DYLD_INTERPOSE(fragmentx_free, free);
FRAGMENTX_DYLD_INTERPOSE(fragmentx_calloc, calloc);
FRAGMENTX_DYLD_INTERPOSE(fragmentx_realloc, realloc);
FRAGMENTX_DYLD_INTERPOSE(fragmentx_aligned_alloc, aligned_alloc);
FRAGMENTX_DYLD_INTERPOSE(fragmentx_posix_memalign, posix_memalign);
