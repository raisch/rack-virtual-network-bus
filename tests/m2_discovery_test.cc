#include "network/discovery.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
  }
  return condition;
}

vcvb::PacketHeader Header(std::uint8_t node, std::uint64_t sequence) {
  vcvb::PacketHeader header;
  header.kind = vcvb::PacketKind::kDnsAnnounce;
  header.node_id[0] = node;
  header.session_id[0] = node;
  header.sequence = sequence;
  return header;
}

vcvb::DiscoveryRecord Record(const char* name) {
  vcvb::DiscoveryRecord record;
  record.node_name = name;
  record.host_name = "test-host";
  record.presence_ttl_ms = 1000;
  record.services_revision = 1;
  vcvb::ServiceDescriptor service;
  service.service_id = "clock-1";
  service.name = "Clock";
  service.type = "vcvb.clock";
  service.port = 47001;
  service.topics.push_back("clock/main");
  record.services.push_back(service);
  return record;
}

bool RegistryTests() {
  vcvb::DiscoveryConfig config;
  config.maximum_ttl_ms = 1000;
  config.inactive_retention_ms = 5000;
  vcvb::DiscoveryRegistry registry(config);
  vcvb::Endpoint endpoint;
  endpoint.address = "127.0.0.1";
  endpoint.port = 47000;
  bool passed = true;

  registry.ProcessAnnouncement(Header(1, 1), Record("studio-main"),
                               endpoint, 0);
  std::vector<vcvb::PeerRecord> peers = registry.Snapshot();
  passed &= Expect(peers.size() == 1, "peer was not registered");
  passed &= Expect(peers[0].status == vcvb::PeerStatus::kActive,
                   "peer was not active");

  registry.Expire(600000000);
  peers = registry.Snapshot();
  passed &= Expect(peers[0].status == vcvb::PeerStatus::kSuspect,
                   "peer did not become suspect");
  registry.Expire(1100000000);
  peers = registry.Snapshot();
  passed &= Expect(peers[0].status == vcvb::PeerStatus::kInactive,
                   "peer did not expire");

  registry.ProcessAnnouncement(Header(1, 2), Record("studio-main"),
                               endpoint, 1200000000);
  peers = registry.Snapshot();
  passed &= Expect(peers[0].status == vcvb::PeerStatus::kActive,
                   "new session did not recover peer");

  registry.ProcessAnnouncement(Header(2, 1), Record("studio-main"),
                               endpoint, 1300000000);
  peers = registry.Snapshot();
  passed &= Expect(peers.size() == 2, "second peer was not registered");
  passed &= Expect(peers[0].status == vcvb::PeerStatus::kConflict &&
                       peers[1].status == vcvb::PeerStatus::kConflict,
                   "duplicate names did not create conflict");
  return passed;
}

bool LiveDiscoveryTest() {
  vcvb::DiscoveryConfig config;
  config.multicast_group = "239.192.0.10";
  config.port = 47123;
  config.heartbeat_interval_ms = 50;
  config.maximum_ttl_ms = 1000;

  vcvb::NodeIdentity first;
  first.node_id[0] = 11;
  first.session_id[0] = 21;
  first.node_name = "m2-first";
  first.host_name = "localhost";
  vcvb::NodeIdentity second;
  second.node_id[0] = 12;
  second.session_id[0] = 22;
  second.node_name = "m2-second";
  second.host_name = "localhost";

  vcvb::DiscoveryManager first_manager(first, config);
  vcvb::DiscoveryManager second_manager(second, config);
  if (!Expect(first_manager.Start(), "first discovery manager failed to start") ||
      !Expect(second_manager.Start(),
              "second discovery manager failed to start")) {
    first_manager.Stop();
    second_manager.Stop();
    return false;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  const std::vector<vcvb::PeerRecord> first_peers = first_manager.Snapshot();
  const std::vector<vcvb::PeerRecord> second_peers = second_manager.Snapshot();
  first_manager.Stop();
  second_manager.Stop();
  return Expect(first_peers.size() == 1 &&
                    first_peers[0].node_name == "m2-second",
                "first manager did not discover second manager") &&
         Expect(second_peers.size() == 1 &&
                    second_peers[0].node_name == "m2-first",
                "second manager did not discover first manager");
}

}  // namespace

int main() {
  const bool passed = RegistryTests() && LiveDiscoveryTest();
  if (passed) {
    std::printf("M2 discovery tests passed\n");
    return 0;
  }
  return 1;
}
