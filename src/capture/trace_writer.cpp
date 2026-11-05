/// @file trace_writer.cpp
/// @brief Binary .fxtrace file writer.

#include <fragmentx/trace.hpp>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <chrono>

#if defined(FRAGMENTX_PLATFORM_LINUX)
#  define PLATFORM_ID 0
#elif defined(FRAGMENTX_PLATFORM_MACOS)
#  define PLATFORM_ID 1
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
#  define PLATFORM_ID 2
#else
#  define PLATFORM_ID 0xFF
#endif

namespace fragmentx {

class FileTraceWriter final : public TraceWriter {
public:
  FileTraceWriter() = default;
  ~FileTraceWriter() override { if (file_) close(); }

  bool open(const std::string& path) override {
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return false;

    // Write header.
    TraceHeader hdr{};
    std::memcpy(hdr.magic, TRACE_MAGIC, 8);
    hdr.format_version    = TRACE_FORMAT_VERSION;
    hdr.pointer_size      = static_cast<uint8_t>(sizeof(void*));
    hdr.platform          = PLATFORM_ID;

    // Timestamps.
    auto now = std::chrono::steady_clock::now();
    hdr.timestamp_base_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count());
    // Random session ID using XOR of pointer and time.
    hdr.session_id = hdr.timestamp_base_ns
                   ^ reinterpret_cast<uintptr_t>(&hdr);

    write_raw(&hdr, sizeof(hdr));
    bytes_written_ += sizeof(hdr);
    return true;
  }

  void write_alloc(const AllocPayload& p) override {
    TraceRecordHeader hdr{};
    hdr.type   = static_cast<uint8_t>(TraceRecordType::Alloc);
    hdr.length = static_cast<uint16_t>(sizeof(p));
    write_raw(&hdr, sizeof(hdr));
    write_raw(&p,   sizeof(p));
    bytes_written_  += sizeof(hdr) + sizeof(p);
    events_written_.fetch_add(1, std::memory_order_relaxed);
  }

  void write_free(const FreePayload& p) override {
    TraceRecordHeader hdr{};
    hdr.type   = static_cast<uint8_t>(TraceRecordType::Free);
    hdr.length = static_cast<uint16_t>(sizeof(p));
    write_raw(&hdr, sizeof(hdr));
    write_raw(&p,   sizeof(p));
    bytes_written_  += sizeof(hdr) + sizeof(p);
    events_written_.fetch_add(1, std::memory_order_relaxed);
  }

  void flush() override {
    if (file_) std::fflush(file_);
  }

  void close() override {
    if (!file_) return;
    // Write end-of-trace sentinel.
    TraceRecordHeader eof{};
    eof.type   = static_cast<uint8_t>(TraceRecordType::EndOfTrace);
    eof.length = 0;
    write_raw(&eof, sizeof(eof));
    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
  }

  uint64_t bytes_written() const noexcept override {
    return bytes_written_;
  }

  uint64_t events_written() const noexcept override {
    return events_written_.load(std::memory_order_relaxed);
  }

private:
  void write_raw(const void* data, std::size_t len) {
    if (file_) std::fwrite(data, 1, len, file_);
  }

  std::FILE*               file_          = nullptr;
  uint64_t                 bytes_written_ = 0;
  std::atomic<uint64_t>    events_written_{0};
};

TraceWriter* TraceWriter::create_file_writer() {
  return new FileTraceWriter{};
}

} // namespace fragmentx

