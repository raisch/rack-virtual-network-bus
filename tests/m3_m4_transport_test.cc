#include "network/transport.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

namespace {
bool Expect(bool value, const char* message) {
  if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
  return value;
}

bool CodecAndRoutingTest() {
  vcvb::PacketHeader header;
  header.kind = vcvb::PacketKind::kGateEvent;
  header.node_id[0] = 4;
  header.session_id[0] = 8;
  header.sequence = 12;
  vcvb::GateEvent event;
  event.stream_id = 99;
  event.event_id = 1234;
  event.channel = 3;
  event.type = vcvb::GateEventType::kTrigger;
  std::uint8_t packet[vcvb::kMaxPacketSize] = {};
  vcvb::CodecResult encoded = vcvb::EncodeGateEventPacket(
      header, "voice/gate", event, packet, sizeof(packet));
  vcvb::PacketHeader decoded_header;
  vcvb::GateEvent decoded_event;
  std::string topic;
  bool passed = Expect(encoded.ok(), "gate event did not encode");
  passed &= Expect(vcvb::DecodeGateEventPacket(packet, encoded.bytes_processed,
                                                &decoded_header, &topic,
                                                &decoded_event).ok(),
                   "gate event did not decode");
  passed &= Expect(topic == "voice/gate" && decoded_event.event_id == 1234 &&
                       decoded_event.channel == 3,
                   "gate event fields changed");
  vcvb::PublisherBinding binding;
  binding.node_id = header.node_id;
  binding.session_id = header.session_id;
  binding.topic = "voice/gate";
  passed &= Expect(binding.Matches(decoded_header, topic), "binding did not match");
  passed &= Expect(!binding.Matches(decoded_header, "Voice/gate"),
                   "topic routing was not exact");
  vcvb::CvState cv;
  cv.channel_count = 16;
  for (std::size_t i = 0; i < cv.channel_count; ++i) cv.voltages[i] = static_cast<float>(i);
  encoded = vcvb::EncodeCvFramePacket(header, "voice/cv", cv, packet, sizeof(packet));
  vcvb::PacketHeader cv_header;
  vcvb::CvState decoded_cv;
  std::string cv_topic;
  passed &= Expect(encoded.ok() &&
                       vcvb::DecodeCvFramePacket(packet, encoded.bytes_processed,
                                                 &cv_header, &cv_topic, &decoded_cv).ok(),
                   "CV frame did not round trip");
  vcvb::PublisherBinding cv_binding = binding;
  cv_binding.topic = "voice/cv";
  vcvb::CvSubscription subscription(cv_binding);
  passed &= Expect(subscription.Accept(cv_header, cv_topic, decoded_cv),
                   "matching CV frame was not queued");
  vcvb::CvFrame frame;
  passed &= Expect(subscription.Pop(&frame) && frame.state.channel_count == 16,
                   "queued CV frame was not reconstructed");
  return passed;
}

bool QueueAndFailureTest() {
  vcvb::BoundedQueue<int, 3> queue;
  bool passed = Expect(queue.Push(1) && queue.Push(2), "queue rejected values");
  passed &= Expect(!queue.Push(3), "queue exceeded its bound");
  int value = 0;
  passed &= Expect(queue.Pop(&value) && value == 1, "queue order changed");

  vcvb::StreamConfig config;
  config.timeout_ms = 10;
  vcvb::GateStreamReceiver receiver(config);
  vcvb::PacketHeader header;
  header.sequence = 1;
  header.session_id[0] = 1;
  vcvb::GateEvent on;
  on.event_id = 7;
  on.channel = 0;
  passed &= Expect(receiver.ApplyEvent(header, on, 1000000), "gate event rejected");
  passed &= Expect(!receiver.ApplyEvent(header, on, 1000000),
                   "duplicate event was accepted");
  passed &= Expect(receiver.output().dropped_duplicates == 1 &&
                       receiver.output().gates[0] == 10.0f,
                   "duplicate changed gate state");
  receiver.Expire(12000000);
  passed &= Expect(receiver.output().timed_out &&
                       receiver.output().gates[0] == 0.0f,
                   "timeout did not fail safe");
  header.session_id[0] = 2;
  header.sequence = 1;
  passed &= Expect(receiver.ApplyEvent(header, on, 13000000),
                   "new session was not accepted");
  return passed;
}

bool LiveBusFanoutTest() {
  vcvb::PublisherBinding binding;
  binding.topic = "integration/cv";
  vcvb::CvSubscription first(binding);
  vcvb::CvSubscription second(binding);
  vcvb::BusRuntime& runtime = vcvb::BusRuntime::Instance();
  runtime.Register(&first);
  runtime.Register(&second);
  vcvb::CvState state;
  state.stream_id = 7;
  state.channel_count = 16;
  for (std::size_t index = 0; index < state.channel_count; ++index)
    state.voltages[index] = static_cast<float>(index) * 0.5f;
  bool passed = Expect(runtime.PublishCv("integration/cv", state),
                       "bus publish was rejected");
  vcvb::CvFrame first_frame;
  vcvb::CvFrame second_frame;
  bool first_received = false;
  bool second_received = false;
  for (int attempt = 0; attempt < 20 &&
                       (!first_received || !second_received); ++attempt) {
    first_received = first_received || first.Pop(&first_frame);
    second_received = second_received || second.Pop(&second_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  passed &= Expect(first_received && second_received &&
                       first_frame.state.channel_count == 16 &&
                       second_frame.state.channel_count == 16,
                   "two subscribers did not receive the CV frame");
  runtime.Unregister(&first);
  runtime.Unregister(&second);
  return passed;
}
}  // namespace

int main() {
  if (CodecAndRoutingTest() && QueueAndFailureTest() &&
      LiveBusFanoutTest()) {
    std::printf("M3/M4 transport tests passed\n");
    return 0;
  }
  return 1;
}
