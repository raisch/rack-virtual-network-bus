#include "network/discovery.h"

#include "network/socket.h"
#include "network/topic.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace vcvb {
namespace {

const std::size_t kMaxDiscoveryStringBytes = 64;
const std::size_t kMaxDiscoveryTopicBytes = 255;

CodecResult DiscoveryResult(ValidationCode code, std::size_t processed) {
  CodecResult result = {code, processed};
  return result;
}

bool IsDiscoveryKind(PacketKind kind) {
  return kind == PacketKind::kDnsAnnounce ||
         kind == PacketKind::kDnsQuery ||
         kind == PacketKind::kDnsResponse ||
         kind == PacketKind::kDnsServiceUpdate ||
         kind == PacketKind::kDnsGoodbye;
}

bool IsValidText(const std::string& value, std::size_t maximum) {
  if (value.empty() || value.size() > maximum) {
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    const unsigned char character =
        static_cast<unsigned char>(value[index]);
    if (character < 0x20u || character == 0x7fu) {
      return false;
    }
  }
  return true;
}

bool WriteString(BoundedWriter* writer, const std::string& value,
                 std::size_t maximum) {
  if (!IsValidText(value, maximum) ||
      value.size() > 255) {
    return false;
  }
  return writer->WriteUInt8(static_cast<std::uint8_t>(value.size())) &&
         writer->WriteBytes(
             reinterpret_cast<const std::uint8_t*>(value.data()),
             value.size());
}

bool ReadString(BoundedReader* reader, std::string* value,
                std::size_t maximum) {
  std::uint8_t length = 0;
  if (!reader->ReadUInt8(&length) || length == 0 ||
      length > maximum) {
    return false;
  }
  std::vector<std::uint8_t> bytes(length);
  if (!reader->ReadBytes(bytes.data(), bytes.size())) {
    return false;
  }
  value->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return IsValidText(*value, maximum);
}

bool WriteService(BoundedWriter* writer, const ServiceDescriptor& service) {
  if (!WriteString(writer, service.service_id, kMaxDiscoveryStringBytes) ||
      !WriteString(writer, service.name, kMaxDiscoveryStringBytes) ||
      !WriteString(writer, service.type, kMaxDiscoveryStringBytes) ||
      !writer->WriteUInt16(service.port) ||
      service.topics.size() > kMaxDiscoveryTopics ||
      !writer->WriteUInt8(static_cast<std::uint8_t>(service.topics.size()))) {
    return false;
  }
  for (std::size_t index = 0; index < service.topics.size(); ++index) {
    if (!IsValidTopic(service.topics[index]) ||
        service.topics[index].size() > kMaxDiscoveryTopicBytes ||
        !WriteString(writer, service.topics[index],
                     kMaxDiscoveryTopicBytes)) {
      return false;
    }
  }
  return true;
}

bool ReadService(BoundedReader* reader, ServiceDescriptor* service) {
  if (!ReadString(reader, &service->service_id, kMaxDiscoveryStringBytes) ||
      !ReadString(reader, &service->name, kMaxDiscoveryStringBytes) ||
      !ReadString(reader, &service->type, kMaxDiscoveryStringBytes) ||
      !reader->ReadUInt16(&service->port)) {
    return false;
  }
  std::uint8_t topic_count = 0;
  if (!reader->ReadUInt8(&topic_count) ||
      topic_count > kMaxDiscoveryTopics) {
    return false;
  }
  service->topics.clear();
  for (std::size_t index = 0; index < topic_count; ++index) {
    std::string topic;
    if (!ReadString(reader, &topic, kMaxDiscoveryTopicBytes) ||
        !IsValidTopic(topic)) {
      return false;
    }
    service->topics.push_back(topic);
  }
  return true;
}

std::uint64_t NowMonotonicNs() {
  const std::chrono::steady_clock::duration elapsed =
      std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

bool SameId(const std::array<std::uint8_t, kIdentifierSize>& left,
            const std::array<std::uint8_t, kIdentifierSize>& right) {
  return left == right;
}

}  // namespace

CodecResult EncodeDiscoveryPacket(const PacketHeader& header,
                                  const DiscoveryRecord& record,
                                  std::uint8_t* buffer,
                                  std::size_t buffer_size) {
  if (buffer == NULL) {
    return DiscoveryResult(ValidationCode::kNullBuffer, 0);
  }
  if (!IsDiscoveryKind(header.kind)) {
    return DiscoveryResult(ValidationCode::kInvalidPacketKind, 0);
  }

  std::array<std::uint8_t, kMaxPayloadSize> payload = {};
  std::size_t payload_size = 0;
  if (header.kind != PacketKind::kDnsQuery) {
    if (!IsValidText(record.node_name, kMaxDiscoveryStringBytes) ||
        !IsValidText(record.host_name, kMaxDiscoveryStringBytes) ||
        record.services.size() > kMaxDiscoveryServices) {
      return DiscoveryResult(ValidationCode::kInvalidDiscoveryRecord, 0);
    }
    BoundedWriter writer(payload.data(), payload.size());
    if (!WriteString(&writer, record.node_name, kMaxDiscoveryStringBytes) ||
        !WriteString(&writer, record.host_name, kMaxDiscoveryStringBytes) ||
        !writer.WriteUInt32(record.presence_ttl_ms) ||
        !writer.WriteUInt32(record.services_revision) ||
        !writer.WriteUInt8(
            static_cast<std::uint8_t>(record.services.size()))) {
      return DiscoveryResult(ValidationCode::kInvalidDiscoveryRecord, 0);
    }
    for (std::size_t index = 0; index < record.services.size(); ++index) {
      if (!WriteService(&writer, record.services[index])) {
        return DiscoveryResult(ValidationCode::kInvalidDiscoveryRecord, 0);
      }
    }
    payload_size = writer.size();
  }

  PacketHeader packet_header = header;
  packet_header.payload_length = static_cast<std::uint16_t>(payload_size);
  CodecResult header_result =
      EncodePacketHeader(packet_header, buffer, buffer_size);
  if (!header_result.ok()) {
    return header_result;
  }
  if (buffer_size < kPacketHeaderSize + payload_size) {
    return DiscoveryResult(ValidationCode::kTruncated, 0);
  }
  if (payload_size != 0) {
    std::memcpy(buffer + kPacketHeaderSize, payload.data(), payload_size);
  }
  return DiscoveryResult(ValidationCode::kOk,
                         kPacketHeaderSize + payload_size);
}

CodecResult DecodeDiscoveryPacket(const std::uint8_t* buffer,
                                  std::size_t buffer_size,
                                  PacketHeader* header,
                                  DiscoveryRecord* record) {
  if (buffer == NULL || header == NULL || record == NULL) {
    return DiscoveryResult(ValidationCode::kNullBuffer, 0);
  }
  CodecResult header_result =
      DecodePacketHeader(buffer, buffer_size, header);
  if (!header_result.ok()) {
    return header_result;
  }
  if (!IsDiscoveryKind(header->kind)) {
    return DiscoveryResult(ValidationCode::kInvalidPacketKind,
                           header_result.bytes_processed);
  }
  *record = DiscoveryRecord();
  if (header->kind == PacketKind::kDnsQuery) {
    if (header->payload_length != 0) {
      return DiscoveryResult(ValidationCode::kInvalidPayloadLength,
                             header_result.bytes_processed);
    }
    return header_result;
  }

  BoundedReader reader(buffer + kPacketHeaderSize, header->payload_length);
  std::uint8_t service_count = 0;
  if (!ReadString(&reader, &record->node_name, kMaxDiscoveryStringBytes) ||
      !ReadString(&reader, &record->host_name, kMaxDiscoveryStringBytes) ||
      !reader.ReadUInt32(&record->presence_ttl_ms) ||
      !reader.ReadUInt32(&record->services_revision) ||
      !reader.ReadUInt8(&service_count) ||
      service_count > kMaxDiscoveryServices ||
      record->presence_ttl_ms == 0) {
    return DiscoveryResult(ValidationCode::kInvalidDiscoveryRecord,
                           header_result.bytes_processed);
  }
  record->services.clear();
  for (std::size_t index = 0; index < service_count; ++index) {
    ServiceDescriptor service;
    if (!ReadService(&reader, &service)) {
      return DiscoveryResult(ValidationCode::kInvalidDiscoveryRecord,
                             header_result.bytes_processed);
    }
    record->services.push_back(service);
  }
  if (reader.position() != header->payload_length) {
    return DiscoveryResult(ValidationCode::kInvalidPayloadLength,
                           header_result.bytes_processed);
  }
  return header_result;
}

DiscoveryRegistry::DiscoveryRegistry(const DiscoveryConfig& config)
    : config_(config) {}

PeerRecord* DiscoveryRegistry::FindPeer(
    const std::array<std::uint8_t, kIdentifierSize>& node_id) {
  for (std::size_t index = 0; index < peers_.size(); ++index) {
    if (SameId(peers_[index].node_id, node_id)) {
      return &peers_[index];
    }
  }
  return NULL;
}

void DiscoveryRegistry::ProcessAnnouncement(const PacketHeader& header,
                                            const DiscoveryRecord& record,
                                            const Endpoint& endpoint,
                                            std::uint64_t now_ns) {
  if (record.presence_ttl_ms == 0) {
    return;
  }
  PeerRecord* peer = FindPeer(header.node_id);
  if (peer == NULL) {
    peers_.push_back(PeerRecord());
    peer = &peers_.back();
    peer->node_id = header.node_id;
  } else if (SameId(peer->session_id, header.session_id) &&
             header.sequence <= peer->last_sequence) {
    return;
  }

  if (!SameId(peer->session_id, header.session_id)) {
    peer->session_id = header.session_id;
    peer->last_sequence = 0;
    peer->inactive_since_monotonic_ns = 0;
  }
  peer->node_name = record.node_name;
  peer->host_name = record.host_name;
  peer->services = record.services;
  peer->effective_ttl_ms =
      std::min(record.presence_ttl_ms, config_.maximum_ttl_ms);
  peer->last_seen_monotonic_ns = now_ns;
  peer->last_sequence = header.sequence;
  peer->status = PeerStatus::kActive;
  peer->inactive_since_monotonic_ns = 0;
  peer->endpoints.clear();
  peer->endpoints.push_back(endpoint);
  RefreshStatuses(now_ns);
}

void DiscoveryRegistry::ProcessGoodbye(const PacketHeader& header,
                                       std::uint64_t now_ns) {
  PeerRecord* peer = FindPeer(header.node_id);
  if (peer == NULL || !SameId(peer->session_id, header.session_id)) {
    return;
  }
  peer->status = PeerStatus::kInactive;
  peer->inactive_since_monotonic_ns = now_ns;
  RefreshStatuses(now_ns);
}

void DiscoveryRegistry::Expire(std::uint64_t now_ns) {
  for (std::size_t index = 0; index < peers_.size();) {
    PeerRecord& peer = peers_[index];
    if (peer.status == PeerStatus::kInactive) {
      if (peer.inactive_since_monotonic_ns != 0 &&
          now_ns - peer.inactive_since_monotonic_ns >=
              static_cast<std::uint64_t>(config_.inactive_retention_ms) *
                  1000000ULL) {
        peers_.erase(peers_.begin() + index);
        continue;
      }
    }
    ++index;
  }
  RefreshStatuses(now_ns);
}

void DiscoveryRegistry::RefreshStatuses(std::uint64_t now_ns) {
  for (std::size_t index = 0; index < peers_.size(); ++index) {
    PeerRecord& peer = peers_[index];
    if (peer.status == PeerStatus::kInactive) {
      continue;
    }
    const std::uint64_t age = now_ns - peer.last_seen_monotonic_ns;
    const std::uint64_t ttl_ns =
        static_cast<std::uint64_t>(peer.effective_ttl_ms) * 1000000ULL;
    if (age >= ttl_ns) {
      peer.status = PeerStatus::kInactive;
      if (peer.inactive_since_monotonic_ns == 0) {
        peer.inactive_since_monotonic_ns = now_ns;
      }
    } else if (age >= ttl_ns * 6 / 10) {
      peer.status = PeerStatus::kSuspect;
    } else {
      peer.status = PeerStatus::kActive;
    }
  }

  for (std::size_t left = 0; left < peers_.size(); ++left) {
    if (peers_[left].status == PeerStatus::kInactive) {
      continue;
    }
    for (std::size_t right = left + 1; right < peers_.size(); ++right) {
      if (peers_[right].status != PeerStatus::kInactive &&
          peers_[left].node_name == peers_[right].node_name) {
        peers_[left].status = PeerStatus::kConflict;
        peers_[right].status = PeerStatus::kConflict;
      }
    }
  }
}

DiscoveryManager::DiscoveryManager(const NodeIdentity& identity,
                                   const DiscoveryConfig& config)
    : identity_(identity),
      config_(config),
      registry_(config),
      services_revision_(0),
      sequence_(0),
      running_(false),
      socket_(new UdpSocket()) {}

DiscoveryManager::~DiscoveryManager() {
  Stop();
}

bool DiscoveryManager::Start() {
  if (running_.load()) {
    return true;
  }
  if (!socket_->Open() || !socket_->Bind(config_.port) ||
      (!config_.interface_address.empty() &&
       !socket_->SetMulticastInterface(config_.interface_address)) ||
      !socket_->JoinMulticast(config_.multicast_group,
                              config_.interface_address)) {
    socket_->Close();
    return false;
  }
  running_.store(true);
  worker_ = std::thread(&DiscoveryManager::Run, this);
  return true;
}

void DiscoveryManager::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  wake_condition_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void DiscoveryManager::SetServices(
    const std::vector<ServiceDescriptor>& services) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  services_ = services;
  ++services_revision_;
}

std::vector<PeerRecord> DiscoveryManager::Snapshot() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return registry_.Snapshot();
}

bool DiscoveryManager::SendDiscoveryPacket(PacketKind kind,
                                            const DiscoveryRecord& record) {
  PacketHeader header;
  header.kind = kind;
  header.node_id = identity_.node_id;
  header.session_id = identity_.session_id;
  header.sequence = ++sequence_;
  std::array<std::uint8_t, kMaxPacketSize> buffer = {};
  CodecResult encoded =
      EncodeDiscoveryPacket(header, record, buffer.data(), buffer.size());
  return encoded.ok() &&
         socket_->SendTo(buffer.data(), encoded.bytes_processed,
                          config_.multicast_group, config_.port);
}

void DiscoveryManager::HandleDatagram(const std::uint8_t* buffer,
                                      std::size_t size,
                                      const std::string& source_address,
                                      std::uint16_t source_port,
                                      std::uint64_t now_ns) {
  PacketHeader header;
  DiscoveryRecord record;
  CodecResult decoded =
      DecodeDiscoveryPacket(buffer, size, &header, &record);
  if (!decoded.ok() || SameId(header.node_id, identity_.node_id)) {
    return;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (header.kind == PacketKind::kDnsQuery) {
    DiscoveryRecord response;
    response.node_name = identity_.node_name;
    response.host_name = identity_.host_name;
    response.presence_ttl_ms = 10000;
    response.services_revision = services_revision_;
    response.services = services_;
    SendDiscoveryPacket(PacketKind::kDnsResponse, response);
    return;
  }
  Endpoint endpoint;
  endpoint.address = source_address;
  endpoint.port = source_port;
  if (header.kind == PacketKind::kDnsGoodbye) {
    registry_.ProcessGoodbye(header, now_ns);
  } else {
    registry_.ProcessAnnouncement(header, record, endpoint, now_ns);
  }
}

void DiscoveryManager::Run() {
  DiscoveryRecord announcement;
  announcement.node_name = identity_.node_name;
  announcement.host_name = identity_.host_name;
  announcement.presence_ttl_ms = 10000;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    announcement.services_revision = services_revision_;
    announcement.services = services_;
  }
  SendDiscoveryPacket(PacketKind::kDnsQuery, DiscoveryRecord());
  SendDiscoveryPacket(PacketKind::kDnsAnnounce, announcement);

  std::uint64_t next_heartbeat_ns =
      NowMonotonicNs() +
      static_cast<std::uint64_t>(config_.heartbeat_interval_ms) * 1000000ULL;
  std::array<std::uint8_t, kMaxPacketSize + 1> buffer = {};
  while (running_.load()) {
    const std::uint64_t now_ns = NowMonotonicNs();
    if (now_ns >= next_heartbeat_ns) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        announcement.services_revision = services_revision_;
        announcement.services = services_;
      }
      SendDiscoveryPacket(PacketKind::kDnsAnnounce, announcement);
      next_heartbeat_ns =
          now_ns + static_cast<std::uint64_t>(
                           config_.heartbeat_interval_ms) *
                       1000000ULL;
    }

    const std::uint64_t until_heartbeat =
        next_heartbeat_ns > now_ns ? next_heartbeat_ns - now_ns : 0;
    const int timeout_ms = static_cast<int>(
        std::min<std::uint64_t>(until_heartbeat / 1000000ULL, 100ULL));
    if (socket_->Poll(timeout_ms)) {
      std::string source_address;
      std::uint16_t source_port = 0;
      const int received =
          socket_->Receive(buffer.data(), buffer.size(), &source_address,
                           &source_port);
      if (received > 0) {
        HandleDatagram(buffer.data(), static_cast<std::size_t>(received),
                       source_address, source_port, NowMonotonicNs());
      }
    }
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      registry_.Expire(NowMonotonicNs());
    }
  }
  socket_->LeaveMulticast(config_.multicast_group,
                          config_.interface_address);
  socket_->Close();
}

}  // namespace vcvb
