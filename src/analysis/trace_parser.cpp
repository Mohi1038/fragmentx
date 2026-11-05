/// @file trace_parser.cpp
/// @brief Parses .fxtrace files into an AllocationDatabase.

#include "trace_parser.hpp"
#include <fragmentx/trace.hpp>
#include <memory>
#include <cstdio>

namespace fragmentx {
namespace analysis {

AllocationDatabase TraceParser::parse(
    const std::string& path,
    std::function<void(std::size_t, std::size_t)> progress)
{
  AllocationDatabase db;
  std::unique_ptr<TraceReader> reader(TraceReader::create_file_reader());

  if (!reader->open(path)) {
    db.error = reader->error();
    return db;
  }

  db.header = reader->header();

  std::size_t callback_count = 0;
  reader->iterate_events([&](const AllocationEvent& ev) -> bool {
    ++callback_count;
    if (progress && (callback_count % 10000 == 0)) {
      progress(callback_count, 0);
    }

    if (ev.type == AllocationEventType::Allocate) {
      db.events.push_back(ev);
      ++db.total_allocs;
      if (!ev.is_freed()) {
        ++db.unmatched_allocs;
        db.live_at_end[ev.address] = db.events.size() - 1;
      }
      db.total_requested_bytes += ev.requested_size;
    }
    return true;
  });

  if (!reader->error().empty()) {
    db.error = reader->error();
    db.events.clear();
    db.live_at_end.clear();
    db.total_allocs = 0;
    db.total_frees = 0;
    db.unmatched_allocs = 0;
    db.total_requested_bytes = 0;
    return db;
  }

  const uint64_t records_read = reader->events_read();
  db.total_frees = records_read > db.total_allocs
      ? records_read - db.total_allocs : 0;
  return db;
}

} // namespace analysis
} // namespace fragmentx

