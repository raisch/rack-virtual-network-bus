#ifndef VIRTUAL_NETWORK_BUS_SRC_NETWORK_DISCOVERY_H_
#define VIRTUAL_NETWORK_BUS_SRC_NETWORK_DISCOVERY_H_

#include "network/protocol.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vcvb {

const std::size_t kMaxDiscoveryServices = 16;
const std::size_t kMaxDiscoveryTopics = 32;

struct ServiceDescriptor {
  std::string service_id;
  std::string name;
  std::string type;
  std::uint16_t port = 0;
  std::vector<std::string> topics;
};

struct DiscoveryRecord {
  std::string node_name;
  std::string host_name;
  std::uint32_t presence_ttl_ms = 10000;
  std::uint32_t services_revision = 0;
  std::vector<ServiceDescriptor> services;
};

struct Endpoint {
  std::string address;
  std::uint16_t port = 0;
};

enum class PeerStatus {
  kActive,
  kSuspect,
  kInactive,
  kConflict,
};

struct PeerRecord {
  std::array<std::uint8_t, kIdentifierSize> node_id = {};
  std::array<std::uint8_t, kIdentifierSize> session_id = {};
  std::string node_name;
  std::string host_name;
  std::vector<Endpoint> endpoints;
  std::vector<ServiceDescriptor> services;
  std::uint64_t last_seen_monotonic_ns = 0;
  std::uint64_t inactive_since_monotonic_ns = 0;
  std::uint64_t last_sequence = 0;
  std::uint32_t effective_ttl_ms = 0;
  PeerStatus status = PeerStatus::kInactive;
};

struct DiscoveryConfig {
  std::string multicast_group = "239.192.0.10";
  std::uint16_t port = 47000;
  std::string interface_address;
  std::uint32_t heartbeat_interval_ms = 2000;
  std::uint32_t maximum_ttl_ms = 30000;
  std::uint32_t inactive_retention_ms = 300000;
};

struct NodeIdentity {
  std::array<std::uint8_t, kIdentifierSize> node_id = {};
  std::array<std::uint8_t, kIdentifierSize> session_id = {};
  std::string node_name;
  std::string host_name;
};

// Encodes a vDNS record as the payload for a discovery packet.
CodecResult EncodeDiscoveryPacket(const PacketHeader& header,
                                  const DiscoveryRecord& record,
                                  std::uint8_t* buffer,
                                  std::size_t buffer_size);

// Decodes a vDNS record from a discovery packet.
CodecResult DecodeDiscoveryPacket(const std::uint8_t* buffer,
                                  std::size_t buffer_size,
                                  PacketHeader* header,
                                  DiscoveryRecord* record);

class DiscoveryRegistry {
 public:
  explicit DiscoveryRegistry(const DiscoveryConfig& config);

  void ProcessAnnouncement(const PacketHeader& header,
                           const DiscoveryRecord& record,
                           const Endpoint& endpoint,
                           std::uint64_t now_monotonic_ns);
  void ProcessGoodbye(const PacketHeader& header,
                      std::uint64_t now_monotonic_ns);
  void Expire(std::uint64_t now_monotonic_ns);

  std::vector<PeerRecord> Snapshot() const { return peers_; }

 private:
  void RefreshStatuses(std::uint64_t now_monotonic_ns);
  PeerRecord* FindPeer(
      const std::array<std::uint8_t, kIdentifierSize>& node_id);
  const DiscoveryConfig config_;
  std::vector<PeerRecord> peers_;
};

class DiscoveryManager {
 public:
  DiscoveryManager(const NodeIdentity& identity, const DiscoveryConfig& config);
  ~DiscoveryManager();

  DiscoveryManager(const DiscoveryManager&) = delete;
  DiscoveryManager& operator=(const DiscoveryManager&) = delete;

  bool Start();
  void Stop();
  bool running() const { return running_.load(); }

  void SetServices(const std::vector<ServiceDescriptor>& services);
  std::vector<PeerRecord> Snapshot() const;

 private:
  void Run();
  bool SendDiscoveryPacket(PacketKind kind,
                           const DiscoveryRecord& record);
  void HandleDatagram(const std::uint8_t* buffer, std::size_t size,
                      const std::string& source_address,
                      std::uint16_t source_port,
                      std::uint64_t now_monotonic_ns);

  const NodeIdentity identity_;
  const DiscoveryConfig config_;
  mutable std::mutex state_mutex_;
  DiscoveryRegistry registry_;
  std::vector<ServiceDescriptor> services_;
  std::uint32_t services_revision_;
  std::uint64_t sequence_;
  std::atomic<bool> running_;
  std::condition_variable wake_condition_;
  mutable std::mutex wake_mutex_;
  std::thread worker_;
  std::unique_ptr<class UdpSocket> socket_;
};

}  // namespace vcvb

#endif  // VIRTUAL_NETWORK_BUS_SRC_NETWORK_DISCOVERY_H_
