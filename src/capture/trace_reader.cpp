/// @file trace_reader.cpp
/// @brief Binary .fxtrace file reader.

#include <fragmentx/trace.hpp>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace fragmentx {

namespace {

struct AllocationKey {
  uint64_t address;
  uint32_t process_id;
  bool operator==(const AllocationKey&) const noexcept = default;
};

struct AllocationKeyHash {
  std::size_t operator()(const AllocationKey& key) const noexcept {
    const std::size_t address_hash = std::hash<uint64_t>{}(key.address);
    const std::size_t process_hash = std::hash<uint32_t>{}(key.process_id);
    return address_hash ^ (process_hash + 0x9e3779b9U + (address_hash << 6U) +
                           (address_hash >> 2U));
  }
};

uint32_t event_process_id(uint64_t thread_id, bool process_scoped) noexcept {
  return process_scoped ? static_cast<uint32_t>(thread_id >> 32U) : 0;
}

} // namespace

class FileTraceReader final : public TraceReader {
public:
  ~FileTraceReader() override {
    if (file_) std::fclose(file_);
  }

  bool open(const std::string& path) override {
    if (file_) { std::fclose(file_); file_ = nullptr; }
    error_.clear();
    saw_end_ = false;
    events_read_.store(0, std::memory_order_relaxed);
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) { error_ = "Cannot open file: " + path; return false; }

    if (std::fread(&header_, sizeof(header_), 1, file_) != 1) {
      error_ = "Truncated header"; std::fclose(file_); file_ = nullptr; return false;
    }
    if (std::memcmp(header_.magic, TRACE_MAGIC, 8) != 0) {
      error_ = "Invalid magic bytes"; std::fclose(file_); file_ = nullptr; return false;
    }
    if ((header_.format_version >> 8) != TRACE_FORMAT_VERSION_MAJOR) {
      error_ = "Incompatible format version"; std::fclose(file_); file_ = nullptr; return false;
    }
    return true;
  }

  TraceHeader header() const noexcept override { return header_; }

  bool next_record(TraceRecordType& out_type,
                   std::vector<uint8_t>& out_payload) override {
    if (!file_ || saw_end_ || !error_.empty()) return false;
    TraceRecordHeader rh{};
    const std::size_t header_bytes = std::fread(&rh, 1, sizeof(rh), file_);
    if (header_bytes != sizeof(rh)) {
      error_ = std::ferror(file_) ? "Error reading record header"
                                  : (header_bytes == 0 ? "Missing end-of-trace sentinel"
                                                       : "Truncated record header");
      return false;
    }
    out_type = static_cast<TraceRecordType>(rh.type);
    if (out_type == TraceRecordType::EndOfTrace) {
      if (rh.length != 0) { error_ = "End-of-trace sentinel has a payload"; return false; }
      saw_end_ = true;
      const int trailing = std::fgetc(file_);
      if (trailing != EOF) error_ = "Trailing data after end-of-trace sentinel";
      else if (std::ferror(file_)) error_ = "Error checking trace terminator";
      return false;
    }

    if ((out_type == TraceRecordType::Alloc && rh.length != sizeof(AllocPayload)) ||
        (out_type == TraceRecordType::Free && rh.length != sizeof(FreePayload))) {
      error_ = "Invalid allocation record size";
      return false;
    }
    out_payload.resize(rh.length);
    if (rh.length > 0 && std::fread(out_payload.data(), 1, rh.length, file_) != rh.length) {
      error_ = "Truncated record"; return false;
    }
    events_read_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  void iterate_events(std::function<bool(const AllocationEvent&)> callback) override {
    if (!file_) return;
    std::clearerr(file_);
    error_.clear();
    saw_end_ = false;
    events_read_.store(0, std::memory_order_relaxed);
    std::rewind(file_);
    // Skip header.
    if (std::fseek(file_, sizeof(TraceHeader), SEEK_SET) != 0) {
      error_ = "Cannot seek to first trace record";
      return;
    }

    // Map address -> partial AllocationEvent (waiting for FREE).
    std::unordered_map<AllocationKey, AllocationEvent, AllocationKeyHash> pending;
    const bool process_scoped =
        (header_.reserved[0] & TRACE_HEADER_FLAG_PROCESS_SCOPED_ADDRESSES) != 0;
    TraceRecordType type{};
    std::vector<uint8_t> payload;
    while (next_record(type, payload)) {
      if (type == TraceRecordType::Alloc && payload.size() >= sizeof(AllocPayload)) {
        AllocPayload p{};
        std::memcpy(&p, payload.data(), sizeof(p));
        AllocationEvent ev{};
        ev.event_id       = p.event_id;
        ev.address        = p.address;
        ev.requested_size = p.requested_size;
        ev.allocated_size = p.allocated_size;
        ev.timestamp_ns   = p.timestamp_ns;
        ev.site_hash      = p.site_hash;
        ev.thread_id      = p.thread_id;
        ev.arena_id       = p.arena_id;
        ev.type           = AllocationEventType::Allocate;
        ev.lifetime_ns    = UINT64_MAX;
        const AllocationKey key{p.address, event_process_id(p.thread_id, process_scoped)};
        if (pending.find(key) != pending.end()) {
          error_ = "Address was allocated again before a matching free event";
          return;
        }
        pending.emplace(key, ev);
      } else if (type == TraceRecordType::Free && payload.size() >= sizeof(FreePayload)) {
        FreePayload p{};
        std::memcpy(&p, payload.data(), sizeof(p));
        const AllocationKey key{p.address, event_process_id(p.thread_id, process_scoped)};
        auto it = pending.find(key);
        if (it != pending.end()) {
          it->second.free_timestamp_ns = p.timestamp_ns;
          it->second.lifetime_ns =
              p.timestamp_ns > it->second.timestamp_ns
              ? p.timestamp_ns - it->second.timestamp_ns : 0;
          if (!callback(it->second)) return;
          pending.erase(it);
        }
      }
    }
    // Emit unmatched (live at trace end).
    for (auto& [key, ev] : pending) {
      (void)key;
      if (!callback(ev)) return;
    }
  }

  uint64_t events_read() const noexcept override {
    return events_read_.load(std::memory_order_relaxed);
  }

  const std::string& error() const noexcept override { return error_; }

private:
  std::FILE*            file_   = nullptr;
  TraceHeader           header_ = {};
  std::string           error_;
  std::atomic<uint64_t> events_read_{0};
  bool                  saw_end_ = false;
};

TraceReader* TraceReader::create_file_reader() {
  return new FileTraceReader{};
}

} // namespace fragmentx

