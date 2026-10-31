#include "network/transport.h"

#include "network/socket.h"

#include <cmath>
#include <cstring>
#include <thread>

namespace vcvb {
namespace {
CodecResult Result(ValidationCode code, std::size_t bytes) {
  CodecResult result = {code, bytes};
  return result;
}
void Put16(std::uint8_t* p, std::uint16_t v) { p[0] = v >> 8; p[1] = v; }
void Put32(std::uint8_t* p, std::uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
void Put64(std::uint8_t* p, std::uint64_t v) {
  for (int i = 7; i >= 0; --i) p[7 - i] = v >> (i * 8);
}
std::uint16_t Get16(const std::uint8_t* p) { return (p[0] << 8) | p[1]; }
std::uint32_t Get32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) | (p[2] << 8) | p[3];
}
std::uint64_t Get64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}
bool ValidTopicField(const std::string& topic) {
  return topic.size() <= 255 && IsValidTopic(topic);
}

bool IsZeroId(const std::array<std::uint8_t, kIdentifierSize>& id) {
  for (std::size_t index = 0; index < id.size(); ++index) {
    if (id[index] != 0) return false;
  }
  return true;
}
CodecResult Begin(PacketKind kind, const PacketHeader& source,
                  const std::string& topic, std::size_t payload,
                  std::uint8_t* buffer, std::size_t size) {
  if (!ValidTopicField(topic)) return Result(ValidationCode::kInvalidDiscoveryRecord, 0);
  if (payload > kMaxPayloadSize || size < kPacketHeaderSize + payload)
    return Result(size < kPacketHeaderSize + payload ? ValidationCode::kTruncated
                                                      : ValidationCode::kOversized, 0);
  PacketHeader header = source;
  header.kind = kind;
  header.payload_length = static_cast<std::uint16_t>(payload);
  return EncodePacketHeader(header, buffer, size);
}
}

bool PublisherBinding::Matches(const PacketHeader& header,
                               const std::string& packet_topic) const {
  return Matches(header, service_id, packet_topic);
}

bool PublisherBinding::Matches(const PacketHeader& header,
                               const std::string& packet_service_id,
                               const std::string& packet_topic) const {
  const bool wildcard = IsZeroId(node_id) && IsZeroId(session_id);
  return (wildcard ||
          (node_id == header.node_id && session_id == header.session_id)) &&
         (service_id.empty() || service_id == packet_service_id) &&
         packet_topic == topic && IsValidTopic(packet_topic);
}

CodecResult EncodeCvFramePacket(const PacketHeader& source, const std::string& topic,
                                const CvState& state, std::uint8_t* buffer,
                                std::size_t size) {
  if (!ValidTopicField(topic) || state.channel_count == 0 ||
      state.channel_count > kMaxCvChannels)
    return Result(ValidationCode::kInvalidPayloadLength, 0);
  for (std::size_t i = 0; i < state.channel_count; ++i)
    if (!std::isfinite(state.voltages[i]))
      return Result(ValidationCode::kNonFiniteVoltage, 0);
  const std::size_t payload = 2 + topic.size() + 20 +
                              state.channel_count * sizeof(float);
  CodecResult r = Begin(PacketKind::kCvState, source, topic, payload, buffer, size);
  if (!r.ok()) return r;
  std::uint8_t* p = buffer + kPacketHeaderSize;
  Put16(p, static_cast<std::uint16_t>(topic.size())); p += 2;
  std::memcpy(p, topic.data(), topic.size()); p += topic.size();
  Put64(p, state.stream_id); p += 8; Put64(p, state.sender_time_ns); p += 8;
  *p++ = state.channel_count; *p++ = state.flags; Put16(p, 0); p += 2;
  for (std::size_t i = 0; i < state.channel_count; ++i) {
    std::uint32_t bits; std::memcpy(&bits, &state.voltages[i], 4); Put32(p, bits); p += 4;
  }
  return Result(ValidationCode::kOk, kPacketHeaderSize + payload);
}

CodecResult DecodeCvFramePacket(const std::uint8_t* buffer, std::size_t size,
                                PacketHeader* header, std::string* topic,
                                CvState* state) {
  if (!buffer || !header || !topic || !state) return Result(ValidationCode::kNullBuffer, 0);
  CodecResult r = DecodePacketHeader(buffer, size, header);
  if (!r.ok()) return r;
  if (header->kind != PacketKind::kCvState || header->payload_length < 22)
    return Result(ValidationCode::kInvalidPacketKind, r.bytes_processed);
  const std::uint8_t* p = buffer + kPacketHeaderSize;
  const std::size_t n = Get16(p); p += 2;
  if (n == 0 || n > 255 || 2 + n + 20 > header->payload_length)
    return Result(ValidationCode::kInvalidPayloadLength, r.bytes_processed);
  topic->assign(reinterpret_cast<const char*>(p), n); p += n;
  if (!ValidTopicField(*topic)) return Result(ValidationCode::kInvalidDiscoveryRecord, r.bytes_processed);
  state->stream_id = Get64(p); p += 8; state->sender_time_ns = Get64(p); p += 8;
  state->channel_count = *p++; state->flags = *p++; p += 2;
  if (state->channel_count == 0 || state->channel_count > kMaxCvChannels ||
      header->payload_length != 2 + n + 20 + state->channel_count * 4)
    return Result(ValidationCode::kInvalidChannelCount, r.bytes_processed);
  for (std::size_t i = 0; i < state->channel_count; ++i) {
    std::uint32_t bits = Get32(p); p += 4; std::memcpy(&state->voltages[i], &bits, 4);
    if (!std::isfinite(state->voltages[i]))
      return Result(ValidationCode::kNonFiniteVoltage, r.bytes_processed);
  }
  return Result(ValidationCode::kOk, r.bytes_processed);
}

CodecResult EncodeGateEventPacket(const PacketHeader& source, const std::string& topic,
                                  const GateEvent& event, std::uint8_t* buffer,
                                  std::size_t size) {
  const std::size_t payload = 2 + topic.size() + 8 + 8 + 8 + 1 + 1 + 4 + 4;
  if (!ValidTopicField(topic) || event.channel >= kMaxCvChannels ||
      !std::isfinite(event.voltage) || event.type < GateEventType::kOn ||
      event.type > GateEventType::kTrigger)
    return Result(ValidationCode::kInvalidPayloadLength, 0);
  CodecResult r = Begin(PacketKind::kGateEvent, source, topic, payload, buffer, size);
  if (!r.ok()) return r;
  std::uint8_t* p = buffer + kPacketHeaderSize;
  Put16(p, static_cast<std::uint16_t>(topic.size())); p += 2;
  std::memcpy(p, topic.data(), topic.size()); p += topic.size();
  Put64(p, event.stream_id); p += 8; Put64(p, event.event_id); p += 8;
  Put64(p, event.sender_time_ns); p += 8; *p++ = static_cast<std::uint8_t>(event.type);
  *p++ = event.channel; Put32(p, event.duration_us); p += 4;
  std::uint32_t bits; std::memcpy(&bits, &event.voltage, 4); Put32(p, bits);
  return Result(ValidationCode::kOk, kPacketHeaderSize + payload);
}

CodecResult DecodeGateEventPacket(const std::uint8_t* buffer, std::size_t size,
                                  PacketHeader* header, std::string* topic,
                                  GateEvent* event) {
  if (!buffer || !header || !topic || !event) return Result(ValidationCode::kNullBuffer, 0);
  CodecResult r = DecodePacketHeader(buffer, size, header);
  if (!r.ok()) return r;
  if (header->kind != PacketKind::kGateEvent || header->payload_length < 2)
    return Result(ValidationCode::kInvalidPacketKind, r.bytes_processed);
  const std::uint8_t* p = buffer + kPacketHeaderSize;
  const std::size_t n = Get16(p); p += 2;
  if (n == 0 || n > 255 || 2 + n + 34 != header->payload_length ||
      kPacketHeaderSize + header->payload_length > size)
    return Result(ValidationCode::kInvalidPayloadLength, r.bytes_processed);
  topic->assign(reinterpret_cast<const char*>(p), n); p += n;
  if (!ValidTopicField(*topic)) return Result(ValidationCode::kInvalidDiscoveryRecord, r.bytes_processed);
  event->stream_id = Get64(p); p += 8; event->event_id = Get64(p); p += 8;
  event->sender_time_ns = Get64(p); p += 8; event->type = static_cast<GateEventType>(*p++);
  event->channel = *p++; event->duration_us = Get32(p); p += 4;
  std::uint32_t bits = Get32(p); std::memcpy(&event->voltage, &bits, 4);
  if (event->channel >= kMaxCvChannels || event->type < GateEventType::kOn ||
      event->type > GateEventType::kTrigger || !std::isfinite(event->voltage))
    return Result(ValidationCode::kInvalidPayloadLength, r.bytes_processed);
  event->sequence = header->sequence;
  return Result(ValidationCode::kOk, r.bytes_processed);
}

CodecResult EncodeGateStatePacket(const PacketHeader& source, const std::string& topic,
                                  const GateState& state, std::uint8_t* buffer,
                                  std::size_t size) {
  const std::size_t payload = 2 + topic.size() + 8 + 8 + 2 + 16 * 4;
  CodecResult r = Begin(PacketKind::kGateState, source, topic, payload, buffer, size);
  if (!r.ok()) return r;
  for (std::size_t i = 0; i < kMaxCvChannels; ++i)
    if (!std::isfinite(state.voltages[i])) return Result(ValidationCode::kNonFiniteVoltage, 0);
  std::uint8_t* p = buffer + kPacketHeaderSize; Put16(p, topic.size()); p += 2;
  std::memcpy(p, topic.data(), topic.size()); p += topic.size(); Put64(p, state.stream_id); p += 8;
  Put64(p, state.sequence); p += 8; Put16(p, state.active_mask); p += 2;
  for (std::size_t i = 0; i < kMaxCvChannels; ++i) { std::uint32_t b; std::memcpy(&b, &state.voltages[i], 4); Put32(p, b); p += 4; }
  return Result(ValidationCode::kOk, kPacketHeaderSize + payload);
}

CodecResult DecodeGateStatePacket(const std::uint8_t* buffer, std::size_t size,
                                  PacketHeader* header, std::string* topic,
                                  GateState* state) {
  if (!buffer || !header || !topic || !state) return Result(ValidationCode::kNullBuffer, 0);
  CodecResult r = DecodePacketHeader(buffer, size, header);
  if (!r.ok()) return r;
  if (header->kind != PacketKind::kGateState || header->payload_length < 2 + 8 + 8 + 2 + 64)
    return Result(ValidationCode::kInvalidPacketKind, r.bytes_processed);
  const std::uint8_t* p = buffer + kPacketHeaderSize; const std::size_t n = Get16(p); p += 2;
  if (n == 0 || n > 255 || 2 + n + 82 != header->payload_length) return Result(ValidationCode::kInvalidPayloadLength, r.bytes_processed);
  topic->assign(reinterpret_cast<const char*>(p), n); p += n; if (!ValidTopicField(*topic)) return Result(ValidationCode::kInvalidDiscoveryRecord, r.bytes_processed);
  state->stream_id = Get64(p); p += 8; state->sequence = Get64(p); p += 8; state->active_mask = Get16(p); p += 2;
  for (std::size_t i = 0; i < kMaxCvChannels; ++i) { std::uint32_t b = Get32(p); p += 4; std::memcpy(&state->voltages[i], &b, 4); if (!std::isfinite(state->voltages[i])) return Result(ValidationCode::kNonFiniteVoltage, r.bytes_processed); }
  return Result(ValidationCode::kOk, r.bytes_processed);
}

GateStreamReceiver::GateStreamReceiver(const StreamConfig& config)
    : config_(config), event_count_(0), last_sequence_(0), last_packet_ns_(0),
      have_session_(false) { session_id_.fill(0); Clear(); }

bool GateStreamReceiver::AcceptIdentity(const PacketHeader& header, std::uint64_t sequence) {
  if (!have_session_ || session_id_ != header.session_id) {
    session_id_ = header.session_id; have_session_ = true; last_sequence_ = 0;
    event_count_ = 0;
  }
  if (sequence <= last_sequence_) { ++output_.dropped_out_of_order; return false; }
  last_sequence_ = sequence; return true;
}
bool GateStreamReceiver::ApplyEvent(const PacketHeader& header, const GateEvent& event,
                                    std::uint64_t now_ns) {
  if (have_session_ && session_id_ != header.session_id) {
    session_id_ = header.session_id;
    event_count_ = 0;
    last_sequence_ = 0;
  }
  for (std::size_t i = 0; i < event_count_; ++i)
    if (event_ids_[i] == event.event_id) { ++output_.dropped_duplicates; return false; }
  if (!AcceptIdentity(header, header.sequence)) return false;
  if (event_count_ < event_ids_.size()) {
    event_ids_[event_count_++] = event.event_id;
  } else {
    event_ids_[last_sequence_ % event_ids_.size()] = event.event_id;
  }
  last_packet_ns_ = now_ns; output_.timed_out = false;
  if (event.type == GateEventType::kOn) output_.gates[event.channel] = event.voltage;
  else if (event.type == GateEventType::kOff) output_.gates[event.channel] = config_.inactive_voltage;
  else ++output_.pending_triggers[event.channel];
  return true;
}
bool GateStreamReceiver::ApplyState(const PacketHeader& header, const GateState& state,
                                    std::uint64_t now_ns) {
  if (!AcceptIdentity(header, header.sequence)) return false;
  for (std::size_t i = 0; i < kMaxCvChannels; ++i)
    output_.gates[i] = (state.active_mask & (1u << i)) ? state.voltages[i] : config_.inactive_voltage;
  last_packet_ns_ = now_ns; output_.timed_out = false; return true;
}
void GateStreamReceiver::Expire(std::uint64_t now_ns) {
  if (last_packet_ns_ != 0 && now_ns - last_packet_ns_ >=
      static_cast<std::uint64_t>(config_.timeout_ms) * 1000000ULL) {
    Clear(); output_.timed_out = true;
  }
}
void GateStreamReceiver::Clear() {
  output_.gates.fill(config_.inactive_voltage); output_.pending_triggers.fill(0);
}

BusRuntime& BusRuntime::Instance() {
  static BusRuntime runtime;
  return runtime;
}

BusRuntime::BusRuntime()
    : running_(false),
      worker_(NULL),
      socket_(new UdpSocket()),
      subscriptions_mutex_(new std::mutex()),
      sequence_(0) {
  node_id_.fill(0);
  session_id_.fill(0);
  node_id_[0] = 0x56;
  node_id_[1] = 0x43;
  session_id_[0] = static_cast<std::uint8_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
}

BusRuntime::~BusRuntime() {
  Stop();
  delete subscriptions_mutex_;
  delete socket_;
}

bool BusRuntime::Start() {
  if (running_.load()) return true;
  if (!socket_->Open() || !socket_->Bind(47001) ||
      !socket_->JoinMulticast("239.192.0.11", "")) {
    socket_->Close();
    return false;
  }
  running_.store(true);
  worker_ = new std::thread(&BusRuntime::Run, this);
  return true;
}

void BusRuntime::Stop() {
  if (!running_.exchange(false)) return;
  if (worker_ != NULL && worker_->joinable()) worker_->join();
  delete worker_;
  worker_ = NULL;
}

bool BusRuntime::PublishCv(const std::string& topic, const CvState& state) {
  if (!running_.load() || topic.size() >= OutboundCvFrame().topic.size()) {
    return false;
  }
  OutboundCvFrame frame;
  std::memcpy(frame.topic.data(), topic.data(), topic.size());
  frame.topic[topic.size()] = '\0';
  frame.state = state;
  return outbound_.Push(frame);
}

void BusRuntime::Register(CvSubscription* subscription) {
  if (subscription == NULL) return;
  Start();
  std::lock_guard<std::mutex> lock(*subscriptions_mutex_);
  subscriptions_.push_back(subscription);
}

void BusRuntime::Unregister(CvSubscription* subscription) {
  std::lock_guard<std::mutex> lock(*subscriptions_mutex_);
  for (std::vector<CvSubscription*>::iterator it = subscriptions_.begin();
       it != subscriptions_.end(); ++it) {
    if (*it == subscription) {
      subscriptions_.erase(it);
      return;
    }
  }
}

void BusRuntime::Run() {
  std::array<std::uint8_t, kMaxPacketSize> packet = {};
  while (running_.load()) {
    OutboundCvFrame outbound;
    if (outbound_.Pop(&outbound)) {
      PacketHeader header;
      header.node_id = node_id_;
      header.session_id = session_id_;
      header.sequence = ++sequence_;
      const std::string topic(outbound.topic.data());
      CodecResult encoded = EncodeCvFramePacket(
          header, topic, outbound.state, packet.data(), packet.size());
      if (encoded.ok()) {
        socket_->SendTo(packet.data(), encoded.bytes_processed,
                        "239.192.0.11", 47001);
      }
    }
    if (socket_->Poll(10)) {
      std::string address;
      std::uint16_t port = 0;
      const int received =
          socket_->Receive(packet.data(), packet.size(), &address, &port);
      if (received > 0) {
        PacketHeader header;
        std::string topic;
        CvState state;
        if (DecodeCvFramePacket(packet.data(), static_cast<std::size_t>(received),
                                &header, &topic, &state).ok()) {
          std::lock_guard<std::mutex> lock(*subscriptions_mutex_);
          for (std::size_t index = 0; index < subscriptions_.size(); ++index) {
            subscriptions_[index]->Accept(header, topic, state);
          }
        }
      }
    }
  }
  socket_->LeaveMulticast("239.192.0.11", "");
  socket_->Close();
}

}  // namespace vcvb
