#pragma once

/// @file trace.hpp
/// @brief Binary trace file format constants and interface types.
///
/// The .fxtrace binary format encodes allocation and deallocation events
/// in a compact, versioned binary stream. See docs/architecture/trace-format.md
/// for a complete byte-level specification.

#include <cstdint>
#include <cstddef>
#include <string>
#include <optional>
#include <functional>
#include <vector>
#include <fragmentx/capture.hpp>

namespace fragmentx {

// ─── Format constants ────────────────────────────────────────────────────────

/// Magic bytes at the start of every .fxtrace file: "FXTRACE\0"
inline constexpr uint8_t TRACE_MAGIC[8] = {
  'F','X','T','R','A','C','E','\0'
};

/// Current binary format version. Increment MINOR for backward-compatible
/// additions; increment MAJOR for breaking changes.
inline constexpr uint16_t TRACE_FORMAT_VERSION_MAJOR = 1;
inline constexpr uint16_t TRACE_FORMAT_VERSION_MINOR = 1;
inline constexpr uint16_t TRACE_FORMAT_VERSION =
    (TRACE_FORMAT_VERSION_MAJOR << 8) | TRACE_FORMAT_VERSION_MINOR;

// ─── Record types ────────────────────────────────────────────────────────────

/// @brief Identifies the type of a trace record.
enum class TraceRecordType : uint8_t {
  Alloc       = 0x01, ///< Memory allocation event.
  Free        = 0x02, ///< Memory deallocation event.
  Realloc     = 0x03, ///< Memory reallocation event.
  SiteTable   = 0x04, ///< Site name table (source location strings).
  Stats       = 0x05, ///< Periodic allocator statistics snapshot.
  Marker      = 0x06, ///< User-supplied annotation marker.
  EndOfTrace  = 0xFF, ///< Sentinel record at the end of a valid trace.
};

// ─── TraceHeader ─────────────────────────────────────────────────────────────

/// @brief Fixed-size header at the start of every .fxtrace file.
/// Total size: 48 bytes.
#pragma pack(push, 1)
struct TraceHeader {
  uint8_t  magic[8];              ///< Must equal TRACE_MAGIC.
  uint16_t format_version;        ///< TRACE_FORMAT_VERSION.
  uint8_t  pointer_size;          ///< sizeof(void*) on the recording platform.
  uint8_t  platform;              ///< 0=Linux, 1=macOS, 2=Windows, 0xFF=unknown.
  uint8_t  reserved[4];           ///< Header flags and reserved bytes.
  uint64_t timestamp_base_ns;     ///< CLOCK_MONOTONIC_RAW at session start.
  uint64_t wall_clock_epoch_ns;   ///< CLOCK_REALTIME at session start (UTC ns).
  uint64_t session_id;            ///< Random 64-bit session identifier.
  uint8_t  reserved2[8];          ///< Reserved; must be zero.
};
#pragma pack(pop)
static_assert(sizeof(TraceHeader) == 48, "TraceHeader must be 48 bytes");

/// `reserved[0]` flag: addresses in allocation/free records are process-scoped
/// by the process ID stored in the upper 32 bits of thread_id.
inline constexpr uint8_t TRACE_HEADER_FLAG_PROCESS_SCOPED_ADDRESSES = 0x01;

/// @brief Variable-length record header preceding each record payload.
#pragma pack(push, 1)
struct TraceRecordHeader {
  uint8_t  type;    ///< TraceRecordType.
  uint16_t length;  ///< Payload length in bytes (not including this header).
};
#pragma pack(pop)
static_assert(sizeof(TraceRecordHeader) == 3, "TraceRecordHeader must be 3 bytes");
#pragma pack(push, 1)
struct AllocPayload {
  uint64_t event_id;
  uint64_t address;
  uint64_t requested_size;
  uint64_t allocated_size;
  uint64_t timestamp_ns;
  uint64_t site_hash;
  uint64_t thread_id;
  uint32_t arena_id;
};  // 60 bytes
struct FreePayload {
  uint64_t event_id;
  uint64_t address;
  uint64_t timestamp_ns;
  uint64_t thread_id;
};  // 32 bytes
#pragma pack(pop)

// ─── TraceWriter interface ───────────────────────────────────────────────────

/// @brief Writes allocation events to a binary .fxtrace file.
class TraceWriter {
public:
  virtual ~TraceWriter() = default;

  /// @brief Open the trace file for writing.
  /// @return true on success.
  [[nodiscard]] virtual bool open(const std::string& path) = 0;

  /// @brief Write an allocation event.
  virtual void write_alloc(const AllocPayload& payload) = 0;

  /// @brief Write a deallocation event.
  virtual void write_free(const FreePayload& payload) = 0;

  /// @brief Flush all buffered data to disk.
  virtual void flush() = 0;

  /// @brief Flush and close the file. Writes EndOfTrace sentinel.
  virtual void close() = 0;

  /// @return Total bytes written to the file.
  [[nodiscard]] virtual uint64_t bytes_written() const noexcept = 0;

  /// @return Total events written.
  [[nodiscard]] virtual uint64_t events_written() const noexcept = 0;

  /// @brief Create a file-backed TraceWriter.
  [[nodiscard]] static TraceWriter* create_file_writer();
};

// ─── TraceReader interface ────────────────────────────────────────────────────

/// @brief Reads and parses a binary .fxtrace file.
class TraceReader {
public:
  virtual ~TraceReader() = default;

  /// @brief Open a trace file for reading.
  /// @return true if the file is a valid .fxtrace file.
  [[nodiscard]] virtual bool open(const std::string& path) = 0;

  /// @brief Read the trace header.
  [[nodiscard]] virtual TraceHeader header() const noexcept = 0;

  /// @brief Read the next raw record header and payload.
  /// @return false when end-of-trace or file error is reached.
  [[nodiscard]] virtual bool next_record(
      TraceRecordType& out_type,
      std::vector<uint8_t>& out_payload) = 0;

  /// @brief Convenience: iterate over matched AllocationEvents.
  ///        Internally matches ALLOC+FREE pairs by address.
  /// @param callback  Called for each matched event. Return false to stop.
  virtual void iterate_events(
      std::function<bool(const AllocationEvent&)> callback) = 0;

  /// @return Total events read so far.
  [[nodiscard]] virtual uint64_t events_read() const noexcept = 0;

  /// @return Human-readable error string if open() or next_record() failed.
  [[nodiscard]] virtual const std::string& error() const noexcept = 0;

  /// @brief Create a file-backed TraceReader.
  [[nodiscard]] static TraceReader* create_file_reader();
};

} // namespace fragmentx

