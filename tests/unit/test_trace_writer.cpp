#include <gtest/gtest.h>
#include <fragmentx/trace.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace fragmentx;

TEST(TraceTest, BinaryRoundTrip) {
  const std::string tmp_file = "test_roundtrip.fxtrace";

  auto writer = TraceWriter::create_file_writer();
  ASSERT_TRUE(writer->open(tmp_file));

  AllocPayload a{};
  a.event_id = 42;
  a.address  = 0x12345678;
  a.requested_size = 120;
  a.allocated_size = 128;
  a.site_hash = 0xABCD;
  writer->write_alloc(a);

  FreePayload f{};
  f.event_id = 43;
  f.address  = 0x12345678;
  writer->write_free(f);

  writer->flush();
  writer->close();
  delete writer;

  auto reader = TraceReader::create_file_reader();
  ASSERT_TRUE(reader->open(tmp_file));

  int count = 0;
  reader->iterate_events([&](const AllocationEvent& ev) {
    count++;
    EXPECT_EQ(ev.address, 0x12345678u);
    EXPECT_EQ(ev.requested_size, 120u);
    EXPECT_EQ(ev.site_hash, 0xABCDu);
    return true;
  });

  EXPECT_EQ(count, 1);
  delete reader;
  std::remove(tmp_file.c_str());
}

TEST(TraceTest, MatchesAddressesWithinEachProcess) {
  const std::string tmp_file = "test_process_scoped.fxtrace";
  auto writer = TraceWriter::create_file_writer();
  ASSERT_TRUE(writer->open(tmp_file));

  AllocPayload first{};
  first.event_id = 1;
  first.address = 0x1234;
  first.requested_size = 16;
  first.timestamp_ns = 10;
  first.thread_id = (101ULL << 32U) | 101ULL;
  writer->write_alloc(first);

  AllocPayload second = first;
  second.event_id = 2;
  second.timestamp_ns = 20;
  second.thread_id = (202ULL << 32U) | 202ULL;
  writer->write_alloc(second);

  FreePayload first_free{};
  first_free.event_id = 3;
  first_free.address = first.address;
  first_free.timestamp_ns = 30;
  first_free.thread_id = first.thread_id;
  writer->write_free(first_free);

  FreePayload second_free = first_free;
  second_free.event_id = 4;
  second_free.timestamp_ns = 40;
  second_free.thread_id = second.thread_id;
  writer->write_free(second_free);
  writer->close();
  delete writer;

  FILE* file = std::fopen(tmp_file.c_str(), "r+b");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(std::fseek(file, static_cast<long>(offsetof(TraceHeader, reserved)), SEEK_SET), 0);
  const uint8_t process_scoped = TRACE_HEADER_FLAG_PROCESS_SCOPED_ADDRESSES;
  ASSERT_EQ(std::fwrite(&process_scoped, sizeof(process_scoped), 1, file), 1u);
  std::fclose(file);

  auto reader = TraceReader::create_file_reader();
  ASSERT_TRUE(reader->open(tmp_file));
  int count = 0;
  reader->iterate_events([&](const AllocationEvent& event) {
    ++count;
    EXPECT_TRUE(event.is_freed());
    EXPECT_EQ(event.lifetime_ns, 20u);
    return true;
  });
  EXPECT_TRUE(reader->error().empty());
  EXPECT_EQ(count, 2);
  delete reader;
  std::remove(tmp_file.c_str());
}

TEST(TraceTest, RejectsTraceWithoutEndSentinel) {
  const std::string tmp_file = "test_missing_sentinel.fxtrace";
  auto writer = TraceWriter::create_file_writer();
  ASSERT_TRUE(writer->open(tmp_file));
  writer->close();
  delete writer;

  const auto size = std::filesystem::file_size(tmp_file);
  ASSERT_GE(size, sizeof(TraceHeader) + sizeof(TraceRecordHeader));
  std::filesystem::resize_file(tmp_file, size - sizeof(TraceRecordHeader));

  auto reader = TraceReader::create_file_reader();
  ASSERT_TRUE(reader->open(tmp_file));
  TraceRecordType type{};
  std::vector<uint8_t> payload;
  while (reader->next_record(type, payload)) {}
  EXPECT_EQ(reader->error(), "Missing end-of-trace sentinel");
  delete reader;
  std::remove(tmp_file.c_str());
}

