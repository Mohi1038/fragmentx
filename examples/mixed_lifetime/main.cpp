#include <fragmentx/fragmentx.hpp>
#include <fragmentx/trace.hpp>
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>

int main(int argc, char** argv) {
  std::string output_file = "mixed_lifetime.fxtrace";
  if (argc > 1) {
    output_file = argv[1];
  }

  std::cout << "FragmentX Canonical Mixed-Lifetime Workload Generator\n";
  std::cout << "Simulating interleaved short-lived and long-lived allocations...\n";

  std::unique_ptr<fragmentx::TraceWriter> writer(fragmentx::TraceWriter::create_file_writer());
  if (!writer->open(output_file)) {
    std::cerr << "Failed to open " << output_file << " for writing\n";
    return 1;
  }

  uint64_t base_time = 1000000000ULL;
  uint64_t event_id  = 1;
  uintptr_t addr_base = 0x7fff00000000ULL;

  uint64_t site_short = 0xDEADBEEF01ULL;
  uint64_t site_long  = 0xCAFEBABE02ULL;

  std::vector<std::pair<uint64_t, uint64_t>> pending_long;

  // 5000 cycles: each cycle allocates 5 short-lived objects (freed immediately)
  // and 1 long-lived object (held until the end)
  for (int cycle = 0; cycle < 5000; ++cycle) {
    base_time += 1000; // +1 µs

    // Short-lived allocations
    for (int s = 0; s < 5; ++s) {
      uint64_t a_addr = addr_base + (cycle * 6 + s) * 256;
      fragmentx::AllocPayload a{};
      a.event_id       = event_id++;
      a.address        = a_addr;
      a.requested_size = 64 + (s * 32);
      a.allocated_size = 128;
      a.timestamp_ns   = base_time;
      a.site_hash      = site_short;
      a.thread_id      = 1;
      a.arena_id       = 1;
      writer->write_alloc(a);

      // Freed 500ns later
      fragmentx::FreePayload f{};
      f.event_id     = event_id++;
      f.address      = a_addr;
      f.timestamp_ns = base_time + 500;
      f.thread_id    = 1;
      writer->write_free(f);
    }

    // Long-lived allocation
    uint64_t l_addr = addr_base + (cycle * 6 + 5) * 256;
    fragmentx::AllocPayload al{};
    al.event_id       = event_id++;
    al.address        = l_addr;
    al.requested_size = 512;
    al.allocated_size = 512;
    al.timestamp_ns   = base_time;
    al.site_hash      = site_long;
    al.thread_id      = 1;
    al.arena_id       = 1;
    writer->write_alloc(al);
    pending_long.push_back({event_id, l_addr});
  }

  // Release long-lived objects at the end of execution
  base_time += 500000000ULL; // 500ms later
  for (const auto& [eid, addr] : pending_long) {
    fragmentx::FreePayload f{};
    f.event_id     = eid;
    f.address      = addr;
    f.timestamp_ns = base_time;
    f.thread_id    = 1;
    writer->write_free(f);
  }

  writer->flush();
  writer->close();

  std::cout << "\033[32m✔ Generated " << writer->events_written()
            << " events into " << output_file << "\033[0m\n";
  std::cout << "You can now run: fragmentx analyze " << output_file << "\n";
  std::cout << "                 fragmentx optimize " << output_file << "\n";

  return 0;
}

