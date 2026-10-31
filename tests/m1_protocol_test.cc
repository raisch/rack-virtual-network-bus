#include "network/protocol.h"
#include "network/topic.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
  }
  return condition;
}

vcvb::PacketHeader MakeHeader() {
  vcvb::PacketHeader header;
  header.kind = vcvb::PacketKind::kCvState;
  header.sequence = 42;
  header.node_id[0] = 1;
  header.session_id[0] = 2;
  return header;
}

bool TopicTests() {
  bool passed = true;
  passed &= Expect(vcvb::IsValidTopic("music/lead/pitch"),
                  "valid topic rejected");
  passed &= Expect(!vcvb::IsValidTopic(""), "empty topic accepted");
  passed &= Expect(!vcvb::IsValidTopic("/music"), "leading slash accepted");
  passed &= Expect(!vcvb::IsValidTopic("music/"), "trailing slash accepted");
  passed &= Expect(!vcvb::IsValidTopic("music//pitch"),
                   "empty topic component accepted");
  passed &= Expect(!vcvb::IsValidTopic("music/\x01pitch"),
                   "control character accepted");
  passed &= Expect(!vcvb::IsValidTopic("\xc0\x80"),
                   "overlong UTF-8 accepted");
  passed &= Expect(vcvb::TopicsMatch("clock/main", "clock/main"),
                  "exact topic did not match");
  passed &= Expect(!vcvb::TopicsMatch("clock/main", "Clock/main"),
                   "case-insensitive topic match");
  return passed;
}

bool HeaderTests() {
  bool passed = true;
  std::uint8_t bounded_buffer[4] = {};
  vcvb::BoundedWriter writer(bounded_buffer, sizeof(bounded_buffer));
  passed &= Expect(writer.WriteUInt16(0x1234), "bounded write failed");
  passed &= Expect(!writer.WriteUInt32(1), "bounded write overflow accepted");
  vcvb::BoundedReader reader(bounded_buffer, writer.size());
  std::uint16_t bounded_value = 0;
  passed &= Expect(reader.ReadUInt16(&bounded_value) &&
                       bounded_value == 0x1234,
                  "bounded read failed");
  std::uint32_t bounded_word = 0;
  passed &= Expect(!reader.ReadUInt32(&bounded_word),
                   "bounded read overflow accepted");

  vcvb::PacketHeader header = MakeHeader();
  std::uint8_t buffer[vcvb::kMaxPacketSize] = {};
  vcvb::CodecResult encoded =
      vcvb::EncodePacketHeader(header, buffer, sizeof(buffer));
  passed &= Expect(encoded.ok(), "header encode failed");
  passed &= Expect(encoded.bytes_processed == vcvb::kPacketHeaderSize,
                  "header size changed");

  vcvb::PacketHeader decoded;
  vcvb::CodecResult decoded_result =
      vcvb::DecodePacketHeader(buffer, encoded.bytes_processed, &decoded);
  passed &= Expect(decoded_result.ok(), "header decode failed");
  passed &= Expect(decoded.sequence == header.sequence,
                  "sequence round trip failed");
  passed &= Expect(decoded.node_id == header.node_id,
                  "node ID round trip failed");

  buffer[0] = 'X';
  passed &= Expect(vcvb::DecodePacketHeader(buffer, sizeof(buffer), &decoded)
                       .code == vcvb::ValidationCode::kBadMagic,
                   "bad magic accepted");
  buffer[0] = 'V';
  passed &= Expect(vcvb::DecodePacketHeader(buffer, 3, &decoded).code ==
                       vcvb::ValidationCode::kTruncated,
                   "truncated header accepted");
  passed &= Expect(vcvb::DecodePacketHeader(buffer, vcvb::kMaxPacketSize + 1,
                                            &decoded)
                       .code == vcvb::ValidationCode::kOversized,
                   "oversized packet accepted");
  header.payload_length =
      static_cast<std::uint16_t>(vcvb::kMaxPayloadSize + 1);
  passed &= Expect(vcvb::EncodePacketHeader(header, buffer, sizeof(buffer))
                       .code == vcvb::ValidationCode::kOversized,
                   "oversized payload accepted");
  return passed;
}

bool CvStateTests() {
  bool passed = true;
  vcvb::PacketHeader header = MakeHeader();
  vcvb::CvState state;
  state.stream_id = 99;
  state.sender_time_ns = 1234;
  state.channel_count = 3;
  state.flags = 7;
  state.voltages[0] = -1.5f;
  state.voltages[1] = 0.0f;
  state.voltages[2] = 10.0f;

  std::uint8_t buffer[vcvb::kMaxPacketSize] = {};
  vcvb::CodecResult encoded =
      vcvb::EncodeCvStatePacket(header, state, buffer, sizeof(buffer));
  passed &= Expect(encoded.ok(), "CV state encode failed");

  vcvb::PacketHeader decoded_header;
  vcvb::CvState decoded_state;
  vcvb::CodecResult decoded = vcvb::DecodeCvStatePacket(
      buffer, encoded.bytes_processed, &decoded_header, &decoded_state);
  passed &= Expect(decoded.ok(), "CV state decode failed");
  passed &= Expect(decoded_state.channel_count == 3,
                  "channel count round trip failed");
  passed &= Expect(decoded_state.voltages[0] == -1.5f &&
                       decoded_state.voltages[2] == 10.0f,
                   "voltage round trip failed");
  passed &= Expect(vcvb::DecodeCvStatePacket(
                       buffer, encoded.bytes_processed - 1, &decoded_header,
                       &decoded_state)
                       .code == vcvb::ValidationCode::kTruncated,
                   "truncated CV state accepted");

  state.voltages[0] = std::numeric_limits<float>::quiet_NaN();
  passed &= Expect(vcvb::EncodeCvStatePacket(header, state, buffer,
                                             sizeof(buffer))
                       .code == vcvb::ValidationCode::kNonFiniteVoltage,
                   "non-finite voltage accepted");
  state.voltages[0] = 0.0f;
  state.channel_count = 17;
  passed &= Expect(vcvb::EncodeCvStatePacket(header, state, buffer,
                                             sizeof(buffer))
                       .code == vcvb::ValidationCode::kInvalidChannelCount,
                   "oversized channel count accepted");

  return passed;
}

}  // namespace

int main() {
  const bool passed = TopicTests() && HeaderTests() && CvStateTests();
  if (passed) {
    std::printf("M1 protocol tests passed\n");
    return 0;
  }
  return 1;
}
