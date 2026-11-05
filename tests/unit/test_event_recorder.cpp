#include <gtest/gtest.h>
#include <fragmentx/trace.hpp>
#include "capture/event_recorder.hpp"

#include <cstdio>
#include <thread>
#include <vector>

TEST(EventRecorderTest, OverflowDoesNotBlockDrainSequence) {
  constexpr uint64_t kEventsPerProducer = 10000;
  constexpr uint64_t kProducerCount = 4;
  constexpr uint64_t kTotalEvents = kEventsPerProducer * kProducerCount;
  const std::string path = "test_event_recorder_overflow.fxtrace";

  fragmentx::capture::EventRecorder recorder(64, path);
  ASSERT_TRUE(recorder.start());

  std::vector<std::thread> producers;
  for (uint64_t producer = 0; producer < kProducerCount; ++producer) {
    producers.emplace_back([producer, &recorder]() {
      for (uint64_t i = 0; i < kEventsPerProducer; ++i) {
        fragmentx::AllocPayload payload{};
        payload.event_id = producer * kEventsPerProducer + i;
        payload.address = payload.event_id + 1;
        payload.requested_size = 32;
        recorder.record_alloc(payload);
      }
    });
  }
  for (auto& producer : producers) producer.join();
  recorder.stop();

  EXPECT_EQ(recorder.recorded_count() + recorder.dropped_count(), kTotalEvents);
  auto reader = fragmentx::TraceReader::create_file_reader();
  ASSERT_TRUE(reader->open(path));
  fragmentx::TraceRecordType type{};
  std::vector<uint8_t> payload;
  while (reader->next_record(type, payload)) {}
  EXPECT_TRUE(reader->error().empty());
  EXPECT_EQ(reader->events_read(), recorder.recorded_count());
  delete reader;
  std::remove(path.c_str());
}
