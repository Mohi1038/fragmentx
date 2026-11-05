// capture_types.hpp — Shared POD types for the FragmentX capture engine.
//
// All structs are trivially copyable and layout-stable. The binary trace
// format relies on the exact byte layout of TraceRecord payloads.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2024 FragmentX Contributors

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <optional>

namespace fxcap {

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

#if defined(__linux__)
#  define FX_PLATFORM_LINUX  1
#  define FX_PLATFORM_ID     0x01
#elif defined(__APPLE__)
#  define FX_PLATFORM_MACOS  1
#  define FX_PLATFORM_ID     0x02
#elif defined(_WIN32)
#  define FX_PLATFORM_WINDOWS 1
#  define FX_PLATFORM_ID      0x03
#else
#  define FX_PLATFORM_ID      0xFF
#endif

// Pointer size in bytes encoded in the header.
static constexpr uint8_t k_pointer_size = static_cast<uint8_t>(sizeof(void*));

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

enum class CaptureError : uint32_t {
    Ok               = 0,
    FileOpenFailed   = 1,
    FileWriteFailed  = 2,
    FileReadFailed   = 3,
    InvalidMagic     = 4,
    VersionMismatch  = 5,
    Truncated        = 6,
    UnknownRecord    = 7,  // non-fatal: caller skips
    RingBufferFull   = 8,
    NotInitialized   = 9,
    AlreadyStarted   = 10,
    CorruptRecord    = 11,
};

// Lightweight expected-like wrapper (no exceptions).
template <typename T>
struct Expected {
    bool      ok;
    T         value;
    CaptureError error;

    static Expected<T> success(T v)             { return {true,  std::move(v), CaptureError::Ok}; }
    static Expected<T> failure(CaptureError e)  { return {false, T{},          e}; }

    explicit operator bool() const { return ok; }
    T&       operator*()           { return value; }
    const T& operator*() const     { return value; }
};

template <>
struct Expected<void> {
    bool ok;
    CaptureError error;

    static Expected<void> success()             { return {true,  CaptureError::Ok}; }
    static Expected<void> failure(CaptureError e){ return {false, e}; }

    explicit operator bool() const { return ok; }
};

// ---------------------------------------------------------------------------
// AllocationEvent — the canonical in-memory representation
// ---------------------------------------------------------------------------

// Operation type stored in the ring buffer and exposed to consumers.
enum class AllocOp : uint8_t {
    Alloc   = 0x01,
    Free    = 0x02,
    Realloc = 0x03,
};

struct AllocationEvent {
    uint64_t event_id;         // monotonically increasing sequence number
    AllocOp  op;               // Alloc / Free / Realloc
    uint8_t  _pad[7];

    uint64_t address;          // returned pointer (Alloc/Realloc) or freed pointer (Free)
    uint64_t old_address;      // previous pointer for Realloc, 0 otherwise
    uint64_t requested_size;   // bytes requested by caller
    uint64_t allocated_size;   // bytes actually allocated (may be > requested)
    uint64_t timestamp_ns;     // nanoseconds since session base time (CLOCK_MONOTONIC_RAW)
    uint64_t site_hash;        // hash of return address / call site
    uint64_t thread_id;        // std::this_thread::get_id() as uint64_t
    uint32_t arena_id;         // allocator arena index (0 if unknown)
    uint32_t _pad2;
};

// ---------------------------------------------------------------------------
// Trace file binary format constants
// ---------------------------------------------------------------------------

// Magic number: "FXTRACE\0" (8 bytes, null-terminated for safety)
static constexpr char     k_magic[8]         = {'F','X','T','R','A','C','E','\0'};
static constexpr uint16_t k_format_version   = 0x0001;

// Record type tags
enum class RecordType : uint8_t {
    Alloc   = 0x01,
    Free    = 0x02,
    Realloc = 0x03,
    Marker  = 0x04,
    Stats   = 0x05,
};

// ---------------------------------------------------------------------------
// On-disk trace header (little-endian, packed)
// ---------------------------------------------------------------------------
// Offset  Size  Field
//   0       8   magic[8]              "FXTRACE\0"
//   8       2   format_version        0x0001
//  10       1   platform              FX_PLATFORM_ID
//  11       1   pointer_size          4 or 8
//  12       8   timestamp_base_ns     wall-clock base for relative timestamps
//  20      16   reserved              zero-filled, for future fields
//  36                                 (total header = 36 bytes)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct TraceHeader {
    char     magic[8];            // "FXTRACE\0"
    uint16_t format_version;      // k_format_version
    uint8_t  platform;            // FX_PLATFORM_ID
    uint8_t  pointer_size;        // sizeof(void*)
    uint64_t timestamp_base_ns;   // absolute epoch of session start (ns)
    uint8_t  reserved[16];        // future use, must be zero
};
static_assert(sizeof(TraceHeader) == 36, "TraceHeader size mismatch");

// ---------------------------------------------------------------------------
// Generic record envelope
// ---------------------------------------------------------------------------
// Offset  Size  Field
//   0       1   type       RecordType tag
//   1       2   length     payload length in bytes (not counting this header)
//   3       ?   payload    type-dependent bytes
// ---------------------------------------------------------------------------

struct RecordEnvelope {
    uint8_t  type;     // RecordType
    uint16_t length;   // payload length
    // payload follows immediately in the file
};
static_assert(sizeof(RecordEnvelope) == 3, "RecordEnvelope size mismatch");

// ---------------------------------------------------------------------------
// ALLOC payload (RecordType::Alloc)
// ---------------------------------------------------------------------------
// Offset  Size  Field
//   0       8   event_id          global sequence number
//   8       8   address           returned pointer
//  16       8   requested_size    bytes requested
//  24       8   allocated_size    bytes actually allocated
//  32       8   timestamp_ns      ns since timestamp_base_ns
//  40       8   site_hash         call-site hash
//  48       8   thread_id         thread identifier
//  56       4   arena_id          allocator arena
//  60       4   _pad              zero
//  64                             total = 64 bytes
// ---------------------------------------------------------------------------

struct AllocPayload {
    uint64_t event_id;
    uint64_t address;
    uint64_t requested_size;
    uint64_t allocated_size;
    uint64_t timestamp_ns;
    uint64_t site_hash;
    uint64_t thread_id;
    uint32_t arena_id;
    uint32_t _pad;
};
static_assert(sizeof(AllocPayload) == 64, "AllocPayload size mismatch");

// ---------------------------------------------------------------------------
// FREE payload (RecordType::Free)
// ---------------------------------------------------------------------------
// Offset  Size  Field
//   0       8   event_id
//   8       8   address           freed pointer
//  16       8   timestamp_ns
//  24       8   thread_id
//  32                             total = 32 bytes
// ---------------------------------------------------------------------------

struct FreePayload {
    uint64_t event_id;
    uint64_t address;
    uint64_t timestamp_ns;
    uint64_t thread_id;
};
static_assert(sizeof(FreePayload) == 32, "FreePayload size mismatch");

// ---------------------------------------------------------------------------
// REALLOC payload (RecordType::Realloc)
// ---------------------------------------------------------------------------
// Offset  Size  Field
//   0       8   event_id
//   8       8   old_address
//  16       8   new_address
//  24       8   old_size          (may be 0 if unknown)
//  32       8   new_requested_size
//  40       8   new_allocated_size
//  48       8   timestamp_ns
//  56       8   site_hash
//  64       8   thread_id
//  72       4   arena_id
//  76       4   _pad
//  80                             total = 80 bytes
// ---------------------------------------------------------------------------

struct ReallocPayload {
    uint64_t event_id;
    uint64_t old_address;
    uint64_t new_address;
    uint64_t old_size;
    uint64_t new_requested_size;
    uint64_t new_allocated_size;
    uint64_t timestamp_ns;
    uint64_t site_hash;
    uint64_t thread_id;
    uint32_t arena_id;
    uint32_t _pad;
};
static_assert(sizeof(ReallocPayload) == 80, "ReallocPayload size mismatch");

// ---------------------------------------------------------------------------
// MARKER payload (RecordType::Marker)
// ---------------------------------------------------------------------------
// Used for site-name table entries and user-defined annotations.
// Offset  Size  Field
//   0       8   site_hash
//   8       4   line_number
//  12       4   file_len          length of file_name UTF-8 string
//  16       4   func_len          length of function_name UTF-8 string
//  20       4   _pad
//  24       ?   file_name         UTF-8, file_len bytes, no null terminator
//  24+file ?   func_name         UTF-8, func_len bytes, no null terminator
// (variable length, length field in envelope covers everything)
// ---------------------------------------------------------------------------

struct MarkerPayloadHeader {
    uint64_t site_hash;
    uint32_t line_number;
    uint32_t file_len;
    uint32_t func_len;
    uint32_t _pad;
};
static_assert(sizeof(MarkerPayloadHeader) == 24, "MarkerPayloadHeader size mismatch");

// ---------------------------------------------------------------------------
// STATS payload (RecordType::Stats)
// ---------------------------------------------------------------------------
// Offset  Size  Field
//   0       8   total_allocs
//   8       8   total_frees
//  16       8   total_reallocs
//  24       8   dropped_events
//  32       8   peak_live_bytes    (best effort)
//  40       8   capture_overhead_ns
//  48                              total = 48 bytes
// ---------------------------------------------------------------------------

struct StatsPayload {
    uint64_t total_allocs;
    uint64_t total_frees;
    uint64_t total_reallocs;
    uint64_t dropped_events;
    uint64_t peak_live_bytes;
    uint64_t capture_overhead_ns;
};
static_assert(sizeof(StatsPayload) == 48, "StatsPayload size mismatch");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// CaptureConfig — session-wide settings
// ---------------------------------------------------------------------------

enum class OverflowPolicy : uint8_t {
    Drop  = 0,  // drop events when ring buffer is full (default, lowest latency)
    Block = 1,  // spin-wait until space is available
};

struct CaptureConfig {
    std::string  output_path;                        // .fxtrace output path
    size_t       ring_buffer_capacity  = 65536;      // must be power-of-2
    size_t       write_buffer_bytes    = 65536;      // trace-writer I/O buffer
    OverflowPolicy overflow_policy     = OverflowPolicy::Drop;
    bool         enable_site_registry  = true;       // resolve symbols to names
    bool         interposition_mode    = false;      // Linux LD_PRELOAD path
};

} // namespace fxcap
