#ifndef VIRTUAL_NETWORK_BUS_SRC_NETWORK_PROTOCOL_H_
#define VIRTUAL_NETWORK_BUS_SRC_NETWORK_PROTOCOL_H_

#include <array>
#include <cstddef>
#include <cstdint>

namespace vcvb {

const std::size_t kMaxPacketSize = 1400;
const std::size_t kPacketHeaderSize = 52;
const std::size_t kMaxPayloadSize = kMaxPacketSize - kPacketHeaderSize;
const std::size_t kIdentifierSize = 16;
const std::size_t kMaxCvChannels = 16;

enum class PacketKind : std::uint8_t {
  kDnsAnnounce = 1,
  kDnsQuery = 2,
  kDnsResponse = 3,
  kDnsServiceUpdate = 4,
  kDnsGoodbye = 5,
  kCvState = 6,
  kGateEvent = 7,
  kGateState = 8,
  kControlStateRequest = 9,
};

enum class ValidationCode {
  kOk,
  kNullBuffer,
  kTruncated,
  kOversized,
  kBadMagic,
  kUnsupportedVersion,
  kInvalidPacketKind,
  kInvalidHeaderLength,
  kInvalidPayloadLength,
  kInvalidChannelCount,
  kNonFiniteVoltage,
  kInvalidDiscoveryRecord,
};

struct CodecResult {
  ValidationCode code;
  std::size_t bytes_processed;

  bool ok() const { return code == ValidationCode::kOk; }
};

struct PacketHeader {
  std::uint8_t version = 1;
  PacketKind kind = PacketKind::kCvState;
  std::uint16_t flags = 0;
  std::uint16_t header_length = kPacketHeaderSize;
  std::uint16_t payload_length = 0;
  std::array<std::uint8_t, kIdentifierSize> node_id = {};
  std::array<std::uint8_t, kIdentifierSize> session_id = {};
  std::uint64_t sequence = 0;
};

struct CvState {
  std::uint64_t stream_id = 0;
  std::uint64_t sender_time_ns = 0;
  std::uint8_t channel_count = 0;
  std::uint8_t flags = 0;
  std::array<float, kMaxCvChannels> voltages = {};
};

class BoundedWriter {
 public:
  BoundedWriter(std::uint8_t* buffer, std::size_t capacity);

  bool WriteUInt8(std::uint8_t value);
  bool WriteUInt16(std::uint16_t value);
  bool WriteUInt32(std::uint32_t value);
  bool WriteUInt64(std::uint64_t value);
  bool WriteBytes(const std::uint8_t* source, std::size_t size);

  std::size_t size() const { return offset_; }

 private:
  std::uint8_t* buffer_;
  std::size_t capacity_;
  std::size_t offset_;
};

class BoundedReader {
 public:
  BoundedReader(const std::uint8_t* buffer, std::size_t size);

  bool ReadUInt8(std::uint8_t* value);
  bool ReadUInt16(std::uint16_t* value);
  bool ReadUInt32(std::uint32_t* value);
  bool ReadUInt64(std::uint64_t* value);
  bool ReadBytes(std::uint8_t* destination, std::size_t size);

  std::size_t position() const { return offset_; }

 private:
  const std::uint8_t* buffer_;
  std::size_t size_;
  std::size_t offset_;
};

// Encodes a validated packet header into buffer.
CodecResult EncodePacketHeader(const PacketHeader& header, std::uint8_t* buffer,
                               std::size_t buffer_size);

// Decodes and validates a packet header from buffer.
CodecResult DecodePacketHeader(const std::uint8_t* buffer,
                               std::size_t buffer_size, PacketHeader* header);

// Encodes a complete CV_STATE packet into buffer.
CodecResult EncodeCvStatePacket(const PacketHeader& header, const CvState& state,
                                std::uint8_t* buffer,
                                std::size_t buffer_size);

// Decodes and validates a complete CV_STATE packet from buffer.
CodecResult DecodeCvStatePacket(const std::uint8_t* buffer,
                                std::size_t buffer_size,
                                PacketHeader* header, CvState* state);

}  // namespace vcvb

#endif  // VIRTUAL_NETWORK_BUS_SRC_NETWORK_PROTOCOL_H_
