#include <gtest/gtest.h>
#include <fragmentx/capture.hpp>
#include <fragmentx/trace.hpp>

#include <cstdio>
#include <memory>

TEST(CaptureSessionTest, SourceHooksWriteAllocationAndFreeRecords) {
  const std::string path = "test_capture_session.fxtrace";
  fragmentx::Config config;
  config.capture_output_path = path;
  fragmentx::CaptureSession session(config);
  ASSERT_TRUE(session.start());

  int token = 0;
  fragmentx::capture::hook_alloc(&token, sizeof(token), 0x1234, 7);
  fragmentx::capture::hook_free(&token, 7);
  session.stop();

  std::unique_ptr<fragmentx::TraceReader> reader(
      fragmentx::TraceReader::create_file_reader());
  ASSERT_TRUE(reader->open(path));
  int allocation_count = 0;
  reader->iterate_events([&](const fragmentx::AllocationEvent& event) {
    ++allocation_count;
    EXPECT_TRUE(event.is_freed());
    EXPECT_EQ(event.requested_size, sizeof(token));
    EXPECT_EQ(event.site_hash, 0x1234u);
    return true;
  });
  EXPECT_TRUE(reader->error().empty());
  EXPECT_EQ(allocation_count, 1);
  EXPECT_EQ(reader->events_read(), 2u);
  std::remove(path.c_str());
}
