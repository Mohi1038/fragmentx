/// @file interposition.cpp
/// @brief Linux LD_PRELOAD allocation tracer.
///
/// This shim deliberately writes records with syscalls and fixed-size stack
/// objects. It must not use FragmentX's allocator or stdio from malloc hooks.

#if !defined(__linux__)
#  error "FragmentX process interposition is currently Linux-only"
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
#include <dlfcn.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>

namespace {

using MallocFn = void* (*)(std::size_t);
using FreeFn = void (*)(void*);
using ReallocFn = void* (*)(void*, std::size_t);
using CallocFn = void* (*)(std::size_t, std::size_t);
using MemalignFn = void* (*)(std::size_t, std::size_t);
using PosixMemalignFn = int (*)(void**, std::size_t, std::size_t);

MallocFn real_malloc = nullptr;
FreeFn real_free = nullptr;
ReallocFn real_realloc = nullptr;
CallocFn real_calloc = nullptr;
MemalignFn real_memalign = nullptr;
PosixMemalignFn real_posix_memalign = nullptr;

thread_local bool resolving = false;
thread_local bool in_hook = false;
std::atomic<int> resolve_state{0};
std::atomic<uint64_t> next_event{0};
std::atomic<bool> write_failed{false};
int trace_fd = -1;
pid_t session_owner_pid = 0;

// dlsym can allocate while resolving malloc. Serve those rare bootstrap
// requests from bounded static storage; such pointers are intentionally never
// passed to libc free.
alignas(std::max_align_t) unsigned char bootstrap[64 * 1024];
std::atomic<std::size_t> bootstrap_used{0};

bool is_bootstrap(const void* ptr) noexcept {
  auto p = reinterpret_cast<uintptr_t>(ptr);
  auto begin = reinterpret_cast<uintptr_t>(bootstrap);
  return p >= begin && p < begin + sizeof(bootstrap);
}

void* bootstrap_alloc(std::size_t size) noexcept {
  const std::size_t alignment = alignof(std::max_align_t);
  constexpr std::size_t header_size = sizeof(std::size_t);
  constexpr std::size_t header_span =
      (header_size + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
  if (size > SIZE_MAX - alignment - header_span) return nullptr;
  const std::size_t payload_size = (size + alignment - 1) & ~(alignment - 1);
  const std::size_t total_size = header_span + payload_size;
  std::size_t old = bootstrap_used.load(std::memory_order_relaxed);
  while (old <= sizeof(bootstrap) && total_size <= sizeof(bootstrap) - old) {
    if (bootstrap_used.compare_exchange_weak(old, old + total_size,
          std::memory_order_relaxed, std::memory_order_relaxed)) {
      auto* result = bootstrap + old + header_span;
      std::memcpy(result - header_size, &size, header_size);
      return result;
    }
  }
  return nullptr;
}

void resolve_symbols() noexcept {
  if (resolving) return;
  int expected = 0;
  if (!resolve_state.compare_exchange_strong(expected, 1,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
    while (resolve_state.load(std::memory_order_acquire) == 1) {}
    return;
  }
  resolving = true;
  real_malloc = reinterpret_cast<MallocFn>(::dlsym(RTLD_NEXT, "malloc"));
  real_free = reinterpret_cast<FreeFn>(::dlsym(RTLD_NEXT, "free"));
  real_realloc = reinterpret_cast<ReallocFn>(::dlsym(RTLD_NEXT, "realloc"));
  real_calloc = reinterpret_cast<CallocFn>(::dlsym(RTLD_NEXT, "calloc"));
  real_memalign = reinterpret_cast<MemalignFn>(::dlsym(RTLD_NEXT, "memalign"));
  real_posix_memalign = reinterpret_cast<PosixMemalignFn>(
      ::dlsym(RTLD_NEXT, "posix_memalign"));
  resolving = false;
  resolve_state.store(2, std::memory_order_release);
}

uint64_t now_ns() noexcept {
  timespec ts{};
  if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) return 0;
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t current_thread_id() noexcept {
  const uint64_t pid = static_cast<uint64_t>(::syscall(SYS_getpid));
  const uint64_t tid = static_cast<uint64_t>(::syscall(SYS_gettid));
  return (pid << 32U) | (tid & 0xffffffffULL);
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
    const ssize_t written = static_cast<ssize_t>(::syscall(SYS_write, trace_fd, cursor, size));
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return false;
    cursor += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

void write_event(const fragmentx::TraceRecordHeader& header, void* payload,
                 std::size_t payload_size, uint64_t& event_id) noexcept {
  const uint64_t pid = static_cast<uint64_t>(::syscall(SYS_getpid));
  event_id = (pid << 32U) | (next_event.fetch_add(1, std::memory_order_relaxed) & 0xffffffffULL);
  iovec records[2]{{const_cast<fragmentx::TraceRecordHeader*>(&header), sizeof(header)},
                   {payload, payload_size}};
  in_hook = true;
  ssize_t written;
  do { written = static_cast<ssize_t>(::syscall(SYS_writev, trace_fd, records, 2)); }
  while (written < 0 && errno == EINTR);
  in_hook = false;
  if (written != static_cast<ssize_t>(sizeof(header) + payload_size))
    write_failed.store(true, std::memory_order_relaxed);
}

void emit_alloc(void* address, std::size_t requested, const void* callsite) noexcept {
  if (trace_fd < 0 || !address || in_hook || resolving) return;
  in_hook = true;
  fragmentx::TraceRecordHeader header{};
  header.type = static_cast<uint8_t>(fragmentx::TraceRecordType::Alloc);
  header.length = static_cast<uint16_t>(sizeof(fragmentx::AllocPayload));
  fragmentx::AllocPayload payload{};
  payload.address = reinterpret_cast<uint64_t>(address);
  payload.requested_size = requested;
  payload.allocated_size = 0; // libc's usable size is allocator-specific.
  payload.timestamp_ns = now_ns();
  payload.site_hash = hash_site(callsite);
  payload.thread_id = current_thread_id();
  write_event(header, &payload, sizeof(payload), payload.event_id);
  in_hook = false;
}

void emit_free(void* address) noexcept {
  if (trace_fd < 0 || !address || in_hook || resolving) return;
  in_hook = true;
  fragmentx::TraceRecordHeader header{};
  header.type = static_cast<uint8_t>(fragmentx::TraceRecordType::Free);
  header.length = static_cast<uint16_t>(sizeof(fragmentx::FreePayload));
  fragmentx::FreePayload payload{};
  payload.address = reinterpret_cast<uint64_t>(address);
  payload.timestamp_ns = now_ns();
  payload.thread_id = current_thread_id();
  write_event(header, &payload, sizeof(payload), payload.event_id);
  in_hook = false;
}

} // namespace

extern "C" __attribute__((constructor)) void fragmentx_capture_init() noexcept {
  const char* output = ::getenv("FRAGMENTX_TRACE_OUTPUT");
  if (!output || !*output) return;
  const char* mode = ::getenv("FRAGMENTX_TRACE_MODE");
  const bool append = mode && std::strcmp(mode, "append") == 0;
  trace_fd = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, output,
                                        O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC |
                                        (append ? 0 : O_TRUNC), 0644));
  if (trace_fd < 0) return;
  const char* owner = ::getenv("FRAGMENTX_TRACE_OWNER_PID");
  session_owner_pid = owner ? static_cast<pid_t>(std::strtol(owner, nullptr, 10))
                            : static_cast<pid_t>(::syscall(SYS_getpid));
  if (session_owner_pid <= 0) session_owner_pid = static_cast<pid_t>(::syscall(SYS_getpid));
  if (append) {
    const off_t end = static_cast<off_t>(::syscall(SYS_lseek, trace_fd, 0, SEEK_END));
    if (end > 0) next_event.store(static_cast<uint64_t>(end), std::memory_order_relaxed);
    return;
  }

  fragmentx::TraceHeader header{};
  std::memcpy(header.magic, fragmentx::TRACE_MAGIC, sizeof(header.magic));
  header.format_version = fragmentx::TRACE_FORMAT_VERSION;
  header.pointer_size = static_cast<uint8_t>(sizeof(void*));
  header.platform = 0;
  header.reserved[0] = fragmentx::TRACE_HEADER_FLAG_PROCESS_SCOPED_ADDRESSES;
  header.timestamp_base_ns = now_ns();
  timespec wall{};
  if (::clock_gettime(CLOCK_REALTIME, &wall) == 0) {
    header.wall_clock_epoch_ns = static_cast<uint64_t>(wall.tv_sec) * 1'000'000'000ULL +
                                 static_cast<uint64_t>(wall.tv_nsec);
  }
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

extern "C" __attribute__((destructor)) void fragmentx_capture_fini() noexcept {
  if (trace_fd < 0) return;
  if (static_cast<pid_t>(::syscall(SYS_getpid)) == session_owner_pid &&
      !write_failed.load(std::memory_order_relaxed)) {
    fragmentx::TraceRecordHeader end{};
    end.type = static_cast<uint8_t>(fragmentx::TraceRecordType::EndOfTrace);
    (void)write_all(&end, sizeof(end));
  }
  ::syscall(SYS_close, trace_fd);
  trace_fd = -1;
}

extern "C" void* malloc(std::size_t size) noexcept {
  if (resolving) return bootstrap_alloc(size);
  resolve_symbols();
  if (!real_malloc) { errno = ENOMEM; return nullptr; }
  void* result = real_malloc(size);
  emit_alloc(result, size, __builtin_return_address(0));
  return result;
}

extern "C" void free(void* ptr) noexcept {
  if (!ptr || is_bootstrap(ptr)) return;
  if (resolving) return;
  resolve_symbols();
  if (!real_free) return;
  emit_free(ptr);
  real_free(ptr);
}

extern "C" void* calloc(std::size_t count, std::size_t size) noexcept {
  if (resolving) {
    if (count != 0 && size > SIZE_MAX / count) return nullptr;
    void* ptr = bootstrap_alloc(count * size);
    if (ptr) std::memset(ptr, 0, count * size);
    return ptr;
  }
  resolve_symbols();
  if (!real_calloc) { errno = ENOMEM; return nullptr; }
  void* result = real_calloc(count, size);
  if (count == 0 || size <= SIZE_MAX / count)
    emit_alloc(result, count * size, __builtin_return_address(0));
  return result;
}

extern "C" void* realloc(void* ptr, std::size_t size) noexcept {
  if (is_bootstrap(ptr)) {
    void* result = malloc(size);
    if (result) {
      std::size_t old_size = 0;
      std::memcpy(&old_size, static_cast<unsigned char*>(ptr) - sizeof(old_size), sizeof(old_size));
      std::memcpy(result, ptr, old_size < size ? old_size : size);
    }
    return result;
  }
  if (resolving) return nullptr;
  resolve_symbols();
  if (!real_realloc) { errno = ENOMEM; return nullptr; }
  void* result = real_realloc(ptr, size);
  if (result) {
    if (ptr) emit_free(ptr);
    emit_alloc(result, size, __builtin_return_address(0));
  } else if (size == 0) {
    emit_free(ptr);
  }
  return result;
}

extern "C" void* reallocarray(void* ptr, std::size_t count,
                               std::size_t size) noexcept {
  if (count != 0 && size > SIZE_MAX / count) { errno = ENOMEM; return nullptr; }
  return realloc(ptr, count * size);
}

extern "C" void* memalign(std::size_t alignment, std::size_t size) noexcept {
  resolve_symbols();
  if (!real_memalign) { errno = ENOMEM; return nullptr; }
  void* result = real_memalign(alignment, size);
  emit_alloc(result, size, __builtin_return_address(0));
  return result;
}

extern "C" int posix_memalign(void** ptr, std::size_t alignment,
                               std::size_t size) noexcept {
  resolve_symbols();
  if (!real_posix_memalign) return ENOMEM;
  const int status = real_posix_memalign(ptr, alignment, size);
  if (status == 0) emit_alloc(*ptr, size, __builtin_return_address(0));
  return status;
}

extern "C" void* aligned_alloc(std::size_t alignment, std::size_t size) noexcept {
  if (alignment == 0 || size % alignment != 0) { errno = EINVAL; return nullptr; }
  return memalign(alignment, size);
}

extern "C" void* valloc(std::size_t size) noexcept {
  return memalign(static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)), size);
}

extern "C" void* pvalloc(std::size_t size) noexcept {
  const long page = ::sysconf(_SC_PAGESIZE);
  if (page <= 0) { errno = ENOMEM; return nullptr; }
  const auto page_size = static_cast<std::size_t>(page);
  if (size > SIZE_MAX - page_size + 1) { errno = ENOMEM; return nullptr; }
  const std::size_t rounded = ((size + page_size - 1) / page_size) * page_size;
  return memalign(page_size, rounded);
}
