#include "network/protocol.h"

#include <cmath>
#include <cstring>

namespace vcvb {
namespace {

const std::uint8_t kMagic[] = {'V', 'C', 'V', 'B'};
const std::uint8_t kProtocolVersion = 1;
const std::size_t kCvStateFixedPayloadSize = 20;

void Write16(std::uint8_t* buffer, std::uint16_t value) {
  buffer[0] = static_cast<std::uint8_t>(value >> 8);
  buffer[1] = static_cast<std::uint8_t>(value);
}

void Write32(std::uint8_t* buffer, std::uint32_t value) {
  buffer[0] = static_cast<std::uint8_t>(value >> 24);
  buffer[1] = static_cast<std::uint8_t>(value >> 16);
  buffer[2] = static_cast<std::uint8_t>(value >> 8);
  buffer[3] = static_cast<std::uint8_t>(value);
}

void Write64(std::uint8_t* buffer, std::uint64_t value) {
  for (int offset = 7; offset >= 0; --offset) {
    buffer[7 - offset] =
        static_cast<std::uint8_t>(value >> (offset * 8));
  }
}

std::uint16_t Read16(const std::uint8_t* buffer) {
  return static_cast<std::uint16_t>((buffer[0] << 8) | buffer[1]);
}

std::uint32_t Read32(const std::uint8_t* buffer) {
  return (static_cast<std::uint32_t>(buffer[0]) << 24) |
         (static_cast<std::uint32_t>(buffer[1]) << 16) |
         (static_cast<std::uint32_t>(buffer[2]) << 8) |
         static_cast<std::uint32_t>(buffer[3]);
}

std::uint64_t Read64(const std::uint8_t* buffer) {
  std::uint64_t value = 0;
  for (int offset = 0; offset < 8; ++offset) {
    value = (value << 8) | buffer[offset];
  }
  return value;
}

bool IsKnownPacketKind(std::uint8_t value) {
  return value >= static_cast<std::uint8_t>(PacketKind::kDnsAnnounce) &&
         value <=
             static_cast<std::uint8_t>(PacketKind::kControlStateRequest);
}

bool IsFiniteState(const CvState& state) {
  if (state.channel_count == 0 ||
      state.channel_count > kMaxCvChannels) {
    return false;
  }
  for (std::size_t index = 0; index < state.channel_count; ++index) {
    if (!std::isfinite(state.voltages[index])) {
      return false;
    }
  }
  return true;
}

CodecResult Result(ValidationCode code, std::size_t bytes_processed) {
  CodecResult result = {code, bytes_processed};
  return result;
}

}  // namespace

BoundedWriter::BoundedWriter(std::uint8_t* buffer, std::size_t capacity)
    : buffer_(buffer), capacity_(capacity), offset_(0) {}

bool BoundedWriter::WriteUInt8(std::uint8_t value) {
  return WriteBytes(&value, sizeof(value));
}

bool BoundedWriter::WriteUInt16(std::uint16_t value) {
  std::uint8_t bytes[2];
  Write16(bytes, value);
  return WriteBytes(bytes, sizeof(bytes));
}

bool BoundedWriter::WriteUInt32(std::uint32_t value) {
  std::uint8_t bytes[4];
  Write32(bytes, value);
  return WriteBytes(bytes, sizeof(bytes));
}

bool BoundedWriter::WriteUInt64(std::uint64_t value) {
  std::uint8_t bytes[8];
  Write64(bytes, value);
  return WriteBytes(bytes, sizeof(bytes));
}

bool BoundedWriter::WriteBytes(const std::uint8_t* source, std::size_t size) {
  if (size > capacity_ - offset_ ||
      (size != 0 && (buffer_ == NULL || source == NULL))) {
    return false;
  }
  std::memcpy(buffer_ + offset_, source, size);
  offset_ += size;
  return true;
}

BoundedReader::BoundedReader(const std::uint8_t* buffer, std::size_t size)
    : buffer_(buffer), size_(size), offset_(0) {}

bool BoundedReader::ReadUInt8(std::uint8_t* value) {
  return ReadBytes(value, sizeof(*value));
}

bool BoundedReader::ReadUInt16(std::uint16_t* value) {
  std::uint8_t bytes[2];
  if (!ReadBytes(bytes, sizeof(bytes))) {
    return false;
  }
  *value = Read16(bytes);
  return true;
}

bool BoundedReader::ReadUInt32(std::uint32_t* value) {
  std::uint8_t bytes[4];
  if (!ReadBytes(bytes, sizeof(bytes))) {
    return false;
  }
  *value = Read32(bytes);
  return true;
}

bool BoundedReader::ReadUInt64(std::uint64_t* value) {
  std::uint8_t bytes[8];
  if (!ReadBytes(bytes, sizeof(bytes))) {
    return false;
  }
  *value = Read64(bytes);
  return true;
}

bool BoundedReader::ReadBytes(std::uint8_t* destination, std::size_t size) {
  if (size > size_ - offset_ ||
      (size != 0 && (buffer_ == NULL || destination == NULL))) {
    return false;
  }
  std::memcpy(destination, buffer_ + offset_, size);
  offset_ += size;
  return true;
}

CodecResult EncodePacketHeader(const PacketHeader& header, std::uint8_t* buffer,
                               std::size_t buffer_size) {
  if (buffer == NULL) {
    return Result(ValidationCode::kNullBuffer, 0);
  }
  if (buffer_size < kPacketHeaderSize) {
    return Result(ValidationCode::kTruncated, 0);
  }
  if (header.version != kProtocolVersion) {
    return Result(ValidationCode::kUnsupportedVersion, 0);
  }
  if (!IsKnownPacketKind(static_cast<std::uint8_t>(header.kind))) {
    return Result(ValidationCode::kInvalidPacketKind, 0);
  }
  if (header.header_length != kPacketHeaderSize) {
    return Result(ValidationCode::kInvalidHeaderLength, 0);
  }
  if (header.payload_length > kMaxPayloadSize) {
    return Result(ValidationCode::kOversized, 0);
  }

  std::memcpy(buffer, kMagic, sizeof(kMagic));
  buffer[4] = header.version;
  buffer[5] = static_cast<std::uint8_t>(header.kind);
  Write16(buffer + 6, header.flags);
  Write16(buffer + 8, header.header_length);
  Write16(buffer + 10, header.payload_length);
  std::memcpy(buffer + 12, header.node_id.data(), kIdentifierSize);
  std::memcpy(buffer + 28, header.session_id.data(), kIdentifierSize);
  Write64(buffer + 44, header.sequence);
  return Result(ValidationCode::kOk, kPacketHeaderSize);
}

CodecResult DecodePacketHeader(const std::uint8_t* buffer,
                               std::size_t buffer_size, PacketHeader* header) {
  if (buffer == NULL || header == NULL) {
    return Result(ValidationCode::kNullBuffer, 0);
  }
  if (buffer_size < kPacketHeaderSize) {
    return Result(ValidationCode::kTruncated, buffer_size);
  }
  if (buffer_size > kMaxPacketSize) {
    return Result(ValidationCode::kOversized, 0);
  }
  if (std::memcmp(buffer, kMagic, sizeof(kMagic)) != 0) {
    return Result(ValidationCode::kBadMagic, 0);
  }
  if (buffer[4] != kProtocolVersion) {
    return Result(ValidationCode::kUnsupportedVersion, 0);
  }
  if (!IsKnownPacketKind(buffer[5])) {
    return Result(ValidationCode::kInvalidPacketKind, 0);
  }

  header->version = buffer[4];
  header->kind = static_cast<PacketKind>(buffer[5]);
  header->flags = Read16(buffer + 6);
  header->header_length = Read16(buffer + 8);
  header->payload_length = Read16(buffer + 10);
  std::memcpy(header->node_id.data(), buffer + 12, kIdentifierSize);
  std::memcpy(header->session_id.data(), buffer + 28, kIdentifierSize);
  header->sequence = Read64(buffer + 44);

  if (header->header_length != kPacketHeaderSize) {
    return Result(ValidationCode::kInvalidHeaderLength, kPacketHeaderSize);
  }
  if (header->payload_length > kMaxPayloadSize) {
    return Result(ValidationCode::kOversized, kPacketHeaderSize);
  }
  if (kPacketHeaderSize + header->payload_length > buffer_size) {
    return Result(ValidationCode::kTruncated, buffer_size);
  }
  return Result(ValidationCode::kOk,
                kPacketHeaderSize + header->payload_length);
}

CodecResult EncodeCvStatePacket(const PacketHeader& header, const CvState& state,
                                std::uint8_t* buffer,
                                std::size_t buffer_size) {
  if (buffer == NULL) {
    return Result(ValidationCode::kNullBuffer, 0);
  }
  if (state.channel_count == 0 || state.channel_count > kMaxCvChannels) {
    return Result(ValidationCode::kInvalidChannelCount, 0);
  }
  if (!IsFiniteState(state)) {
    return Result(ValidationCode::kNonFiniteVoltage, 0);
  }
  const std::size_t payload_size =
      kCvStateFixedPayloadSize + state.channel_count * sizeof(float);
  if (payload_size > kMaxPayloadSize ||
      buffer_size < kPacketHeaderSize + payload_size) {
    return Result(buffer_size < kPacketHeaderSize + payload_size
                      ? ValidationCode::kTruncated
                      : ValidationCode::kOversized,
                  0);
  }

  PacketHeader packet_header = header;
  packet_header.kind = PacketKind::kCvState;
  packet_header.payload_length = static_cast<std::uint16_t>(payload_size);
  CodecResult header_result =
      EncodePacketHeader(packet_header, buffer, buffer_size);
  if (!header_result.ok()) {
    return header_result;
  }

  Write64(buffer + kPacketHeaderSize, state.stream_id);
  Write64(buffer + kPacketHeaderSize + 8, state.sender_time_ns);
  buffer[kPacketHeaderSize + 16] = state.channel_count;
  buffer[kPacketHeaderSize + 17] = state.flags;
  Write16(buffer + kPacketHeaderSize + 18, 0);
  for (std::size_t index = 0; index < state.channel_count; ++index) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &state.voltages[index], sizeof(bits));
    Write32(buffer + kPacketHeaderSize + kCvStateFixedPayloadSize +
                index * sizeof(float),
            bits);
  }
  return Result(ValidationCode::kOk, kPacketHeaderSize + payload_size);
}

CodecResult DecodeCvStatePacket(const std::uint8_t* buffer,
                                std::size_t buffer_size,
                                PacketHeader* header, CvState* state) {
  if (buffer == NULL || header == NULL || state == NULL) {
    return Result(ValidationCode::kNullBuffer, 0);
  }
  CodecResult header_result =
      DecodePacketHeader(buffer, buffer_size, header);
  if (!header_result.ok()) {
    return header_result;
  }
  if (header->kind != PacketKind::kCvState) {
    return Result(ValidationCode::kInvalidPacketKind,
                  header_result.bytes_processed);
  }
  if (header->payload_length < kCvStateFixedPayloadSize) {
    return Result(ValidationCode::kTruncated, header_result.bytes_processed);
  }

  const std::uint8_t* payload = buffer + kPacketHeaderSize;
  state->stream_id = Read64(payload);
  state->sender_time_ns = Read64(payload + 8);
  state->channel_count = payload[16];
  state->flags = payload[17];
  if (state->channel_count == 0 || state->channel_count > kMaxCvChannels) {
    return Result(ValidationCode::kInvalidChannelCount,
                  header_result.bytes_processed);
  }
  const std::size_t expected_payload_size =
      kCvStateFixedPayloadSize + state->channel_count * sizeof(float);
  if (header->payload_length != expected_payload_size) {
    return Result(header->payload_length < expected_payload_size
                      ? ValidationCode::kTruncated
                      : ValidationCode::kInvalidPayloadLength,
                  header_result.bytes_processed);
  }
  for (std::size_t index = 0; index < state->channel_count; ++index) {
    const std::uint32_t bits =
        Read32(payload + kCvStateFixedPayloadSize + index * sizeof(float));
    std::memcpy(&state->voltages[index], &bits, sizeof(bits));
    if (!std::isfinite(state->voltages[index])) {
      return Result(ValidationCode::kNonFiniteVoltage,
                    header_result.bytes_processed);
    }
  }
  return Result(ValidationCode::kOk, header_result.bytes_processed);
}

}  // namespace vcvb
