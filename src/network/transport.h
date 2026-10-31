#ifndef VIRTUAL_NETWORK_BUS_SRC_NETWORK_TRANSPORT_H_
#define VIRTUAL_NETWORK_BUS_SRC_NETWORK_TRANSPORT_H_

#include "network/protocol.h"
#include "network/topic.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

namespace vcvb {

// A single-producer/single-consumer queue suitable for audio-thread handoff.
// Full queues drop the new item and never allocate or block.
template <typename T, std::size_t Capacity>
class BoundedQueue {
 public:
  BoundedQueue() : head_(0), tail_(0) {}
  bool Push(const T& value) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) % Capacity;
    if (next == tail_.load(std::memory_order_acquire)) return false;
    values_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }
  bool Pop(T* value) {
    if (value == NULL) return false;
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    *value = values_[tail];
    tail_.store((tail + 1) % Capacity, std::memory_order_release);
    return true;
  }
  std::size_t size() const {
    const std::size_t h = head_.load(std::memory_order_acquire);
    const std::size_t t = tail_.load(std::memory_order_acquire);
    return (h + Capacity - t) % Capacity;
  }

 private:
  static_assert(Capacity > 1, "queue capacity must leave one sentinel slot");
  std::array<T, Capacity> values_;
  std::atomic<std::size_t> head_;
  std::atomic<std::size_t> tail_;
};

struct PublisherBinding {
  std::array<std::uint8_t, kIdentifierSize> node_id = {};
  std::array<std::uint8_t, kIdentifierSize> session_id = {};
  std::string service_id;
  std::string topic;
  bool Matches(const PacketHeader& header, const std::string& packet_topic) const;
  bool Matches(const PacketHeader& header, const std::string& packet_service_id,
              const std::string& packet_topic) const;
};

struct CvFrame {
  PacketHeader header;
  CvState state;
  std::array<char, 256> topic = {};
};

CodecResult EncodeCvFramePacket(const PacketHeader& header,
                                const std::string& topic,
                                const CvState& state,
                                std::uint8_t* buffer, std::size_t size);
CodecResult DecodeCvFramePacket(const std::uint8_t* buffer, std::size_t size,
                                PacketHeader* header, std::string* topic,
                                CvState* state);

class CvSubscription {
 public:
  explicit CvSubscription(const PublisherBinding& binding) : binding_(binding) {}
  void SetTopic(const std::string& topic) { binding_.topic = topic; }
  bool Accept(const PacketHeader& header, const std::string& topic,
              const CvState& state) {
    if (!binding_.Matches(header, topic)) {
      rejected_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    CvFrame frame;
    frame.header = header;
    frame.state = state;
    std::memcpy(frame.topic.data(), topic.data(), topic.size());
    if (!queue_.Push(frame)) {
      overflow_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return true;
  }
  bool Pop(CvFrame* frame) { return queue_.Pop(frame); }
  std::uint64_t rejected() const {
    return rejected_.load(std::memory_order_relaxed);
  }
  std::uint64_t overflow() const {
    return overflow_.load(std::memory_order_relaxed);
  }

 private:
  PublisherBinding binding_;
  BoundedQueue<CvFrame, 32> queue_;
  std::atomic<std::uint64_t> rejected_{0};
  std::atomic<std::uint64_t> overflow_{0};
};

struct OutboundCvFrame {
  std::array<char, 256> topic = {};
  CvState state;
};

class BusRuntime {
 public:
  static BusRuntime& Instance();

  bool Start();
  void Stop();
  bool PublishCv(const std::string& topic, const CvState& state);
  void Register(CvSubscription* subscription);
  void Unregister(CvSubscription* subscription);

 private:
  BusRuntime();
  ~BusRuntime();
  BusRuntime(const BusRuntime&) = delete;
  BusRuntime& operator=(const BusRuntime&) = delete;
  void Run();

  std::atomic<bool> running_;
  std::thread* worker_;
  class UdpSocket* socket_;
  BoundedQueue<OutboundCvFrame, 64> outbound_;
  std::mutex* subscriptions_mutex_;
  std::vector<CvSubscription*> subscriptions_;
  std::array<std::uint8_t, kIdentifierSize> node_id_;
  std::array<std::uint8_t, kIdentifierSize> session_id_;
  std::uint64_t sequence_;
};

enum class GateEventType : std::uint8_t { kOn = 1, kOff = 2, kTrigger = 3 };

struct GateEvent {
  std::uint64_t stream_id = 0;
  std::uint64_t event_id = 0;
  std::uint64_t sender_time_ns = 0;
  std::uint64_t sequence = 0;
  GateEventType type = GateEventType::kOn;
  std::uint8_t channel = 0;
  float voltage = 10.0f;
  std::uint32_t duration_us = 1000;
};

struct GateState {
  std::uint64_t stream_id = 0;
  std::uint64_t sequence = 0;
  std::uint16_t active_mask = 0;
  std::array<float, kMaxCvChannels> voltages = {};
};

CodecResult EncodeGateEventPacket(const PacketHeader& header,
                                  const std::string& topic,
                                  const GateEvent& event,
                                  std::uint8_t* buffer, std::size_t size);
CodecResult DecodeGateEventPacket(const std::uint8_t* buffer, std::size_t size,
                                  PacketHeader* header, std::string* topic,
                                  GateEvent* event);
CodecResult EncodeGateStatePacket(const PacketHeader& header,
                                  const std::string& topic,
                                  const GateState& state,
                                  std::uint8_t* buffer, std::size_t size);
CodecResult DecodeGateStatePacket(const std::uint8_t* buffer, std::size_t size,
                                  PacketHeader* header, std::string* topic,
                                  GateState* state);

struct StreamConfig {
  std::uint32_t timeout_ms = 500;
  std::uint32_t late_tolerance_ms = 10;
  float active_voltage = 10.0f;
  float inactive_voltage = 0.0f;
};

struct StreamOutput {
  std::array<float, kMaxCvChannels> gates = {};
  std::array<std::uint32_t, kMaxCvChannels> pending_triggers = {};
  bool timed_out = false;
  std::uint64_t dropped_duplicates = 0;
  std::uint64_t dropped_out_of_order = 0;
};

// Applies ordered, session-fenced events. All state is caller-owned and
// therefore can be copied into a Rack output without locking.
class GateStreamReceiver {
 public:
  explicit GateStreamReceiver(const StreamConfig& config);
  bool ApplyEvent(const PacketHeader& header, const GateEvent& event,
                  std::uint64_t now_ns);
  bool ApplyState(const PacketHeader& header, const GateState& state,
                  std::uint64_t now_ns);
  void Expire(std::uint64_t now_ns);
  void Clear();
  const StreamOutput& output() const { return output_; }

 private:
  bool AcceptIdentity(const PacketHeader& header, std::uint64_t sequence);
  StreamConfig config_;
  StreamOutput output_;
  std::array<std::uint64_t, 32> event_ids_;
  std::size_t event_count_;
  std::uint64_t last_sequence_;
  std::uint64_t last_packet_ns_;
  std::array<std::uint8_t, kIdentifierSize> session_id_;
  bool have_session_;
};

}  // namespace vcvb

#endif
