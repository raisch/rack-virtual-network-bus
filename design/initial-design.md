# VCV Network Bus
## Initial System Design — v0.1

**Components:** vDNS (Virtual Discovery and Naming Service), vCVB (Virtual Control Voltage Bus)  
**Target platform:** VCV Rack 2, C++ plugin API  
**Transport:** UDP multicast, with optional UDP unicast  
**Status:** Preliminary architecture  
**Date:** October 31, 2026

---

# 1. Executive Summary

VCV Network Bus is a distributed networking system for VCV Rack that allows multiple instances of Rack, running on the same or different computers, to discover one another and exchange control signals and structured messages over a local-area network.

The system comprises two closely related protocols:

**vDNS — Virtual Discovery and Naming Service**

A decentralized service-discovery protocol that maintains a dynamic registry of available hosts, services, and advertised topics. Every participating instance announces its presence periodically through UDP multicast. Peers maintain local registries and automatically mark hosts inactive when their announcements expire.

**vCVB — Virtual Control Voltage Bus**

A topic-addressed messaging protocol for transmitting control voltages, gates, triggers, and structured control messages between Rack instances. Topics provide logical addressing independent of the physical network addresses of participating hosts.

The initial implementation will support:

- Automatic peer and service discovery.
- Human-readable host and service names.
- Local resolution of names to IP addresses and UDP ports.
- Configurable heartbeat intervals and host-expiration timeouts.
- Dynamic lists of active and inactive hosts.
- Service and topic advertisement.
- Named topic subscriptions.
- Polyphonic CV transmission and reception.
- Remote topic-triggered actions.
- UDP multicast distribution.
- Optional unicast delivery.
- Multiple independent Rack instances on one computer.
- Patch-level configuration persistence.
- Basic network diagnostics.

The design requires no external discovery server, DNS configuration, or centralized message broker.

# 2. Goals and Design Principles

## 2.1 Primary goals

1. Make networking between Rack instances nearly as simple as connecting two modules with a cable.
2. Eliminate the need to manually configure remote IP addresses.
3. Allow users to assign meaningful names to Rack instances and their services.
4. Support communication between multiple hosts without requiring pairwise connections.
5. Provide topic-based routing so that remote instances can respond selectively to incoming messages.
6. Preserve Rack's polyphonic CV semantics.
7. Detect disconnected or unavailable hosts automatically.
8. Avoid blocking or otherwise interfering with Rack's real-time audio processing.
9. Permit independent development of discovery and signal-transport functionality.
10. Establish an extensible foundation for future distributed synthesis and sequencing.

## 2.2 Non-goals for v0.1

The initial implementation will not attempt to provide:

- Audio streaming.
- Sample-accurate synchronization across computers.
- Guaranteed delivery of every UDP packet.
- Internet-wide host discovery.
- Automatic traversal of routers or firewalls.
- A distributed database.
- Cryptographic authentication or encryption.
- Full MIDI or MIDI 2.0 transport.
- Remote execution of arbitrary commands.
- Automatic patch replication.
- A general-purpose network file-sharing system.

These may be considered separately in future versions.

# 3. Terminology

| Term | Definition |
|---|---|
| Node | One participating Rack networking instance with a unique identity |
| Host | The computer running one or more nodes |
| Service | A named capability advertised by a node |
| Topic | A hierarchical message-routing identifier |
| Publisher | A node transmitting messages on a topic |
| Subscriber | A node receiving messages matching a topic |
| Registry | A locally maintained directory of discovered nodes and services |
| Heartbeat | A periodic presence announcement |
| TTL | The maximum permitted interval without a qualifying presence message |
| Bus | A logical collection of topic-addressed messages |
| Endpoint | An IP address, transport protocol, and port |
| Session | One runtime incarnation of a node |
| CV frame | A timestamped collection of control-voltage samples |

A node is not necessarily equivalent to a physical computer. A single computer may run multiple Rack processes, and each process may contain multiple network modules.

The vDNS namespace is application-specific and does not replace the operating system's DNS resolver.

# 4. System Architecture

## 4.1 Logical architecture

```text
 COMPUTER A
 ┌───────────────────────────────────┐
 │ VCV Rack                          │
 │                                   │
 │ Sequencer ──┐                     │
 │ LFO ────────┼──► vCVB Publisher   │
 │ Envelope ───┘         │           │
 │                       │           │
 │                 Network Manager   │
 │                       │           │
 │                 vDNS Registry     │
 └───────────────────────┼───────────┘
                         │
                UDP MULTICAST LAN
                         │
           ┌─────────────┴─────────────┐
           │                           │
    COMPUTER B                  COMPUTER C
 ┌─────────────────┐        ┌─────────────────┐
 │ VCV Rack        │        │ VCV Rack        │
 │                 │        │                 │
 │ vDNS Registry   │        │ vDNS Registry   │
 │       │         │        │       │         │
 │ vCVB Subscriber │        │ vCVB Subscriber │
 │       │         │        │       │         │
 │ Synthesizer     │        │ Modulation      │
 └─────────────────┘        └─────────────────┘
```

Each participating process maintains its own discovery registry. Registries are populated by multicast announcements and do not require a central authority.

## 4.2 Software components

### NetworkManager

A process-wide networking service responsible for:

- UDP socket management.
- Multicast group membership.
- Network-interface selection.
- Packet transmission and reception.
- Protocol decoding and validation.
- Routing packets to local modules.
- Maintaining shared discovery state.
- Network statistics and diagnostics.

One NetworkManager should serve all networking modules in the same Rack process.

### DiscoveryManager

Implements vDNS:

- Local node registration.
- Heartbeat scheduling.
- Announcement generation.
- Registry maintenance.
- Service enumeration.
- Name resolution.
- TTL expiration.
- Duplicate-name detection.
- Peer-state transitions.

### BusManager

Implements vCVB:

- Topic publication.
- Topic subscriptions.
- CV-frame serialization.
- Message delivery.
- Stream sequencing.
- Receive buffering.
- Topic filtering.
- Signal reconstruction.

### Rack Module Layer

Provides Rack-facing functionality:

- Module parameters.
- CV input and output ports.
- User interface.
- Context menus.
- Patch persistence.
- Status indicators.
- Communication with NetworkManager.

# 5. vDNS: Discovery and Naming

## 5.1 Purpose

vDNS allows participating Rack instances to locate one another by name rather than requiring explicit IP addresses.

For example, a user may configure:

```text
Node name: studio-main
Service:   sequencer
Topic:     music/lead/pitch
```

Another computer can discover `studio-main`, identify its advertised `sequencer` service, and subscribe to the relevant topic.

## 5.2 Identity model

Each node has the following identity attributes:

| Attribute | Purpose |
|---|---|
| nodeId | Persistent UUID identifying a logical node |
| sessionId | Unique identifier for the current runtime session |
| nodeName | Human-readable node name |
| hostName | Human-readable computer name |
| instanceName | Optional Rack instance label |
| addresses | Reachable IP addresses |
| services | Advertised service descriptors |
| topics | Advertised publication and subscription capabilities |
| protocolVersion | Supported vDNS version |
| lastSeen | Local monotonic timestamp of last qualifying heartbeat |
| status | Active, suspect, inactive, or conflict |

`nodeId` is authoritative for identity. Names are aliases and may change.

A new `sessionId` is generated whenever a node restarts. This distinguishes a restarted process from stale packets belonging to an earlier session.

For v0.1, node identifiers are stored in module configuration. Cloning a module must create a new identifier rather than accidentally duplicating the original.

## 5.3 Namespaces

Suggested naming hierarchy:

```text
network
  └── host
       └── node
            └── service
                 └── topic
```

Example:

```text
studio-main/rack-a/sequencer
studio-main/rack-a/clock
studio-secondary/rack-b/synth
```

Names are intended for human use. Internally, routing and registry updates rely on UUIDs and endpoint identifiers.

Duplicate display names are permitted only when disambiguated by node ID. The UI must never silently resolve an ambiguous name to an arbitrary peer.

## 5.4 Service model

A service describes a capability offered by a node.

Example:

```json
{
  "serviceId": "sequencer-1",
  "name": "Main Sequencer",
  "type": "vcvb.cv.publisher",
  "transport": "udp",
  "port": 47001,
  "topics": [
    "music/lead/pitch",
    "music/lead/gate",
    "music/lead/timbre"
  ]
}
```

Initial service types:

| Type | Meaning |
|---|---|
| vcvb.cv.publisher | Publishes CV data |
| vcvb.cv.subscriber | Receives CV data |
| vcvb.event.publisher | Publishes discrete events |
| vcvb.event.subscriber | Receives discrete events |
| vcvb.clock | Publishes or receives timing signals |
| vcvb.control | Supports structured control messages |
| vdns.discovery | Participates in discovery |

Service descriptors should include stable identifiers, versions, and advertised capabilities.

## 5.5 Multicast configuration

Proposed defaults:

| Setting | Initial default |
|---|---|
| Discovery multicast group | 239.192.0.10 |
| Discovery UDP port | 47000 |
| CV bus multicast group | 239.192.0.11 |
| CV bus UDP port | 47001 |
| Heartbeat interval | 2 seconds |
| Presence TTL | 10 seconds |
| Multicast IP hop limit | 1 |
| Preferred transport | IPv4 UDP |
| Preferred network | Selected LAN interface |
| Discovery scope | Local network |

The multicast addresses and ports are provisional and must be configurable.

The multicast IP hop limit is distinct from the vDNS presence TTL. The former controls network forwarding; the latter determines when a peer is considered unavailable.

IPv6 multicast is a future enhancement.

## 5.6 Heartbeat protocol

Every node periodically transmits a presence announcement.

Example conceptual payload:

```json
{
  "protocol": "vDNS",
  "version": 1,
  "type": "ANNOUNCE",
  "nodeId": "6c4f...",
  "sessionId": "a813...",
  "name": "studio-main",
  "hostName": "synth-mac",
  "sequence": 1042,
  "presenceTtlMs": 10000,
  "servicesRevision": 7,
  "services": [
    {
      "id": "seq-1",
      "type": "vcvb.cv.publisher",
      "port": 47001
    }
  ]
}
```

This JSON illustrates the semantic fields. Production packets should use a versioned, bounded binary encoding.

The heartbeat cadence is configured by the local node.

The receiver determines expiration using its own monotonic clock, not the sender's wall-clock timestamp.

### Presence policy

The receiver applies a locally configured maximum TTL to the sender's advertised TTL:

```text
effectiveTTL =
    min(advertisedTTL, receiverMaximumTTL)
```

The initial receiver maximum should be 30 seconds.

This prevents a misconfigured sender from requesting an indefinitely long presence lifetime.

A valid heartbeat refreshes the node's `lastSeen` timestamp.

Ordinary CV data does not automatically refresh discovery presence in v0.1.

### Lifecycle

```text
UNKNOWN
   │
   ▼
ACTIVE
   │
   │ No heartbeat for warning threshold
   ▼
SUSPECT
   │
   │ No heartbeat for effective TTL
   ▼
INACTIVE
   │
   │ New valid heartbeat
   ▼
ACTIVE
```

The suspect threshold defaults to 60% of effective TTL.

A node can also send an explicit `GOODBYE` announcement during graceful shutdown. A receiver may immediately mark that session inactive, but must not assume graceful shutdown will always occur.

Inactive records remain visible for a configurable retention period, initially five minutes.

After retention expires, the record may be removed unless pinned or explicitly saved by the user.

## 5.7 Service discovery

Services may be advertised in full announcements or obtained through a query-response exchange.

Proposed messages:

- `ANNOUNCE`: Advertise node identity and service revision.
- `QUERY`: Request discovery information.
- `RESPONSE`: Return matching discovery information.
- `SERVICE_UPDATE`: Advertise changed service descriptors.
- `GOODBYE`: Announce graceful departure.

A new node multicasts a query when it starts. Existing nodes respond after a short randomized delay to avoid response bursts.

Service metadata should have bounded sizes. Large service catalogs can be retrieved through unicast queries rather than packed into oversized multicast datagrams.

## 5.8 Local registry

Suggested C++ representation:

```cpp
enum class PeerStatus {
    Active,
    Suspect,
    Inactive,
    Conflict
};

struct Endpoint {
    std::string address;
    uint16_t port;
};

struct ServiceRecord {
    std::string serviceId;
    std::string name;
    std::string type;
    std::vector<std::string> topics;
    std::vector<Endpoint> endpoints;
};

struct PeerRecord {
    UUID nodeId;
    UUID sessionId;

    std::string nodeName;
    std::string hostName;

    std::vector<Endpoint> endpoints;
    std::vector<ServiceRecord> services;

    uint64_t lastSeenMonotonicNs;
    uint32_t effectiveTtlMs;

    PeerStatus status;
};
```

The registry is owned by the network worker, not the audio thread.

Rack modules receive immutable snapshots or bounded notifications of registry changes.

## 5.9 Name resolution

vDNS should expose these conceptual operations:

```text
resolveNode(name)
resolveService(nodeName, serviceName)
findServices(type)
findPublishers(topic)
findSubscribers(topic)
listActiveNodes()
listAllNodes()
```

Resolution returns one or more endpoint records, not simply a string IP address.

The API must handle ambiguous names, multiple network interfaces, and services with multiple endpoints.

# 6. vCVB: Control Voltage Bus

## 6.1 Purpose

vCVB provides a logical messaging bus over UDP.

It supports two primary categories of data:

1. Continuous or periodically sampled control voltages.
2. Discrete messages used to trigger remote behavior.

A publisher sends a message to a topic. Subscribers whose filters match that topic receive the message.

No subscriber-specific connection is required for multicast delivery.

## 6.2 Topic naming

Topics use slash-separated hierarchical names.

Examples:

```text
clock/main
transport/start
transport/stop
transport/reset

music/lead/pitch
music/lead/gate
music/lead/velocity

modulation/lfo1
modulation/envelope1

control/preset/select
control/scene/activate
control/record/start
```

Topic names are case-sensitive UTF-8 strings with a maximum encoded length of 255 bytes.

Empty components, control characters, and ambiguous normalization should be rejected.

For v0.1, exact topic matching is required. Hierarchical wildcard subscriptions are a future enhancement.

## 6.3 Publication and subscription

A module may publish to one or more topics.

A module may also subscribe to one or more topics.

Example:

```text
Computer A
  publishes: modulation/lfo1

Computer B
  subscribes: modulation/lfo1

Computer C
  subscribes: modulation/lfo1
```

Both B and C receive the same stream.

A topic is not permanently bound to an IP address. Discovery metadata identifies publishers, while multicast carries the actual data.

Multiple publishers may use the same topic. For CV subscriptions, the receiver must select a publisher explicitly or apply a documented arbitration policy. The initial default is one selected publisher per subscribed CV port.

## 6.4 Message categories

| Category | Description |
|---|---|
| CV_STATE | Current voltage values |
| CV_EVENT | Gate, trigger, or discrete voltage transition |
| CONTROL | Structured control command |
| TRANSPORT | Start, stop, reset, or position |
| CLOCK | Clock and timing information |
| STATUS | Publisher state and diagnostics |

## 6.5 CV payload

Each CV frame contains:

- Publisher node ID.
- Session ID.
- Topic identifier.
- Stream identifier.
- Sequence number.
- Sender-relative timestamp.
- Channel count.
- Up to 16 voltage values.
- Frame flags.

Conceptual structure:

```cpp
struct CVFrame {
    uint64_t streamId;
    uint64_t sequence;

    uint64_t senderTimeNs;

    uint8_t channelCount;
    uint8_t flags;

    float voltage[16];
};
```

CV values use IEEE 754 binary32 encoding in network byte order.

All values must be finite. NaN and infinity are rejected.

The receiver may clamp output to a configured safe range, with a default corresponding to Rack's usual control-voltage conventions.

## 6.6 Update rates

Initial configurable rates:

| Rate | Intended use |
|---|---|
| 20 Hz | Slow status or parameter changes |
| 50 Hz | Low-rate modulation |
| 100 Hz | General control |
| 250 Hz | Moderate-speed modulation |
| 500 Hz | Default high-resolution CV |
| 1,000 Hz | Fast control modulation |

The publisher samples the input voltages at the selected rate.

The receiver reconstructs the signal using a configurable policy:

- Hold last value.
- Linear interpolation.
- Smoothed transition.

Interpolation requires a small playback buffer and introduces latency.

## 6.7 Gates and Triggers

Gates and triggers require special treatment because they represent discrete musical events rather than continuously varying control voltages.

A gate expresses a persistent logical state, usually high while a note is held and low when it is released. A trigger represents a momentary event, typically a short pulse used to initiate an envelope, advance a sequencer, reset an oscillator, or activate another process.

Periodic CV snapshots alone cannot reliably preserve these behaviors. A trigger may begin and end between successive network updates, while a lost gate-off message could leave a receiving module in a permanently active state.

vCVB therefore treats gates and triggers as distinct event types, while preserving compatibility with ordinary Rack voltage outputs.

### 6.7.1 Event types

The initial protocol defines four event types:

| Event | Description |
|---|---|
| `GATE_ON` | Sets the specified channel to its active voltage |
| `GATE_OFF` | Sets the specified channel to its inactive voltage |
| `TRIGGER` | Generates a momentary pulse on the specified channel |
| `GATE_STATE` | Reports the current gate state for one or more channels |

Gate transitions are transmitted immediately when detected, subject to the network worker's scheduling latency.

Gate state is also included in periodic state-refresh messages, allowing receivers to recover from lost transitions.

Trigger messages carry unique event identifiers so that receivers can detect and discard duplicates.

### 6.7.2 Event representation

A conceptual event structure:

```cpp
enum class EventType : uint8_t {
    GateOn,
    GateOff,
    Trigger,
    GateState
};

struct GateTriggerEvent {
    uint64_t streamId;
    uint64_t sequenceNumber;
    uint64_t eventId;

    uint64_t senderTimestampNs;

    EventType type;

    uint8_t channel;
    float voltage;

    uint32_t durationUs;
};
```

The structure is illustrative; the actual network representation will use explicit serialization rather than transmitting C++ structure memory directly.

Fields have the following meanings:

- `streamId`: Identifies the published signal stream.
- `sequenceNumber`: Detects missing or out-of-order packets.
- `eventId`: Identifies an individual event for duplicate suppression.
- `senderTimestampNs`: Indicates the event's occurrence time in the sender's monotonic clock domain.
- `type`: Specifies the gate or trigger operation.
- `channel`: Identifies the polyphonic channel, numbered 0–15.
- `voltage`: Specifies the active voltage, ordinarily 10V.
- `durationUs`: Specifies the desired trigger pulse duration in microseconds.

A gate event's active state persists until a corresponding gate-off, a newer state snapshot, or a configured failure policy changes it.

A trigger event is transient and does not establish persistent state.

### 6.7.3 Gate transmission

For each published gate stream, the sender maintains the previous logical state of all 16 channels.

On each Rack processing sample, the sender detects transitions using configurable voltage thresholds.

The default thresholds are:

| Parameter | Default |
|---|---|
| Rising threshold | 2.0V |
| Falling threshold | 0.1V |
| Active output voltage | 10.0V |
| Inactive output voltage | 0.0V |

The separate rising and falling thresholds provide hysteresis, preventing noisy signals from generating repeated transitions near a single threshold.

When a channel changes from inactive to active, the sender generates `GATE_ON`.

When a channel changes from active to inactive, the sender generates `GATE_OFF`.

The audio thread places these events in a bounded, preallocated queue. The network worker serializes and transmits them without blocking audio processing.

Transitions must not be coalesced merely because they occur between two periodic CV updates. A short gate pulse may contain both a rising and falling transition within one update interval.

The implementation must preserve event ordering within each stream.

### 6.7.4 Trigger transmission

Trigger detection uses the same configurable rising and falling thresholds as gate detection.

When the input crosses the rising threshold, the sender generates a `TRIGGER` event.

The initial implementation uses one event per detected rising edge.

The receiver generates a locally timed pulse with a configurable duration.

Recommended default:

```text
Trigger output voltage: 10V
Trigger duration:       1 ms
Minimum duration:       0.1 ms
Maximum duration:       100 ms
```

Trigger duration is measured using the receiver's audio sample clock, not network-worker timing.

At a 48 kHz sample rate, a 1 ms pulse corresponds to approximately 48 samples.

A received trigger event should be scheduled for playback by the Rack audio thread. The network worker must never directly manipulate Rack output voltages.

For v0.1, a trigger received while its output is already high follows a configurable retrigger policy:

- **Restart:** Restart the pulse timer, extending the high state.
- **Queue:** Generate another pulse after the current pulse, with at least one low sample between them.
- **Ignore:** Discard triggers received while the output is high.

The default is `Queue`, subject to a bounded queue capacity.

### 6.7.5 Polyphonic gate and trigger behavior

Each gate or trigger stream supports up to 16 independent channels, corresponding to Rack's polyphonic cable model.

Channel identities are preserved across the network.

For example:

```text
Publisher channel 0 → Receiver channel 0
Publisher channel 1 → Receiver channel 1
Publisher channel 2 → Receiver channel 2
...
Publisher channel 15 → Receiver channel 15
```

Every channel maintains independent gate state, trigger timing, and event sequencing where necessary.

A publisher must explicitly announce changes in channel count. When a channel is removed, the receiver releases any active gate associated with that channel before reducing its output channel count.

For the initial implementation, a single publisher owns each subscribed gate or trigger output. Combining events from multiple publishers is deferred to a later version.

### 6.7.6 State refresh and recovery

UDP does not guarantee delivery, ordering, or uniqueness.

The sender therefore periodically transmits `GATE_STATE` messages containing the current logical state of all active channels.

The state may be encoded as a 16-bit mask:

```cpp
struct GateState {
    uint64_t streamId;
    uint64_t sequenceNumber;
    uint64_t senderTimestampNs;

    uint16_t activeChannels;
    uint8_t channelCount;
};
```

Each bit in `activeChannels` corresponds to one polyphonic channel.

A set bit indicates an active gate.

The default state-refresh interval is 100 ms, independently configurable from the ordinary CV update rate.

On receiving a state refresh, the receiver reconciles its local gate states with the publisher's authoritative state.

For example, if a `GATE_OFF` packet was lost, the next valid state refresh will indicate that the corresponding channel is inactive.

To prevent an old snapshot from undoing a newer transition, state snapshots and transitions must share a comparable per-stream ordering mechanism. The receiver discards stale state updates.

A state refresh can recover the current gate level but cannot reconstruct every missed transition. A gate that opened and closed entirely between refreshes may be lost if both transition packets were dropped.

### 6.7.7 Event ordering and duplicate detection

Every publisher maintains a monotonically increasing sequence number for each stream within its current session.

The receiver uses the publisher ID, session ID, stream ID, and sequence number to identify the ordering context.

Incoming events are checked for:

- Duplicate event identifiers.
- Out-of-order delivery.
- Missing sequence numbers.
- Stale timestamps.
- Unknown or expired publisher sessions.

Duplicate triggers are discarded to prevent unintended retriggering.

Events arriving within the configured jitter-buffer window are ordered by their intended playback timestamps, with sequence numbers used to resolve ties.

An event arriving after its playback deadline is handled according to the late-event policy.

Recommended policies:

| Policy | Behavior |
|---|---|
| Immediate | Execute the late event as soon as possible |
| Discard | Ignore events that have missed their deadline |
| Bounded late | Execute only if lateness is below a threshold |

The initial default is `Immediate` for gate-off events and `Bounded late` for gate-on and trigger events.

Gate-off events receive special consideration because dropping them may leave a gate active indefinitely.

### 6.7.8 Packet loss and reliability

vCVB does not guarantee exactly-once delivery.

The initial implementation uses best-effort UDP with:

- Unique event identifiers.
- Per-stream sequence numbers.
- Duplicate suppression.
- Periodic gate-state refresh.
- Receiver-side state reconciliation.
- Timeout-based gate release.
- Diagnostic counters for missing and late events.

For particularly important events, the sender may optionally transmit a small number of redundant copies separated by a configurable interval.

Duplicate suppression ensures that redundant trigger packets do not normally produce multiple pulses.

Redundant transmission is disabled by default because it increases network traffic and does not provide a formal delivery guarantee.

A future protocol version may support selective acknowledgments or reliable unicast delivery for critical control events.

### 6.7.9 Receiver timing

The receiver maintains a small event buffer to compensate for network jitter.

Each incoming event is assigned a local playback deadline using the estimated offset between sender and receiver monotonic clocks.

The audio thread consumes events whose deadlines have been reached.

The default receiver buffering configuration is:

```text
Jitter buffer:          5 ms
Late-event tolerance:   10 ms
Clock-offset tracking:  Enabled
```

Clock-offset estimates are approximate in v0.1. Without synchronized clocks or a bounded network delay, the receiver cannot guarantee sample-accurate alignment with the sender.

Gate and trigger events belonging to the same stream must retain their original logical ordering even if packet arrival order differs.

The receiver must also handle events whose scheduled times fall within the current audio processing interval.

### 6.7.10 Disconnection behavior

If the publisher becomes unavailable, the receiver must avoid leaving gates indefinitely active.

The receiver monitors both:

1. The publisher's vDNS presence state.
2. The freshness of the subscribed vCVB stream.

These are separate conditions.

A publisher may remain discoverable while one of its signal streams has stopped transmitting.

Each subscribed gate stream therefore has an independent activity timeout.

Default behavior:

```text
Gate stream timeout:    500 ms
Publisher inactive:     Release all gates
Network disconnection:  Release all gates
Trigger queue:          Clear pending triggers
```

The stream timeout is measured from the most recent valid stream message, including state refreshes.

When a timeout occurs, all active gates associated with the affected stream transition to their inactive voltage.

The receiver records the timeout as a diagnostic event.

Optional future policies include holding the last state, retaining selected gates, or fading associated continuous CV signals.

The default behavior favors predictable recovery and avoids stuck notes.

### 6.7.11 Startup and reconnection

When a receiver first subscribes to a gate stream, it must not assume that all gates are inactive indefinitely.

The receiver initializes its outputs to 0V and waits for an authoritative state refresh.

Once the first valid state snapshot arrives, it establishes the current gate state.

A newly subscribed receiver may optionally request an immediate state snapshot using a unicast control message.

On reconnection after a publisher restart, the new session ID invalidates event-ordering state from the previous session.

The receiver clears stale queued events and initializes a new stream state.

This prevents delayed packets from an earlier publisher session from activating gates after a restart.

### 6.7.12 Relationship to the vDNS heartbeat

vDNS heartbeats and vCVB gate-state refreshes serve different purposes.

**vDNS heartbeat**

Confirms that a network node and its advertised services remain discoverable.

**vCVB gate-state refresh**

Confirms the current logical state of a particular signal stream.

A healthy vDNS heartbeat does not prove that a vCVB stream is operating correctly.

Likewise, a received CV packet does not automatically refresh the node's vDNS presence TTL.

This separation allows independent monitoring of discovery health and signal-delivery health.

### 6.7.13 Configuration parameters

The module should expose the following configuration options through its context menu or a dedicated settings interface.

| Setting | Default |
|---|---|
| Input mode | Automatic gate/trigger selection |
| Rising threshold | 2.0V |
| Falling threshold | 0.1V |
| Output high voltage | 10.0V |
| Output low voltage | 0.0V |
| Trigger duration | 1 ms |
| Retrigger policy | Queue |
| Gate-state refresh | 100 ms |
| Jitter buffer | 5 ms |
| Late-event tolerance | 10 ms |
| Gate stream timeout | 500 ms |
| Disconnection policy | Release gates |
| Redundant event transmission | Disabled |
| Maximum polyphony | 16 channels |

Automatic input classification is an optional UI convenience and must not silently change the meaning of a stream during playback. Explicit gate or trigger mode should be the reliable default for saved patches.

### 6.7.14 Diagnostics

The receiver should maintain counters for:

- Gate-on events received.
- Gate-off events received.
- Trigger events received.
- Duplicate events discarded.
- Out-of-order packets.
- Missing sequence numbers.
- Late events.
- State reconciliations.
- Gate timeouts.
- Publisher disconnections.
- Trigger queue overflows.
- Event queue overflows.

The UI should display the current stream state and indicate whether synchronization is healthy, degraded, or unavailable.

Diagnostics must not require locks or dynamic allocation in the audio processing thread.

### 6.7.15 Testing requirements

The gate and trigger implementation should be tested under both normal and adverse network conditions.

Required test scenarios include:

1. Single-channel gate-on and gate-off.
2. Simultaneous polyphonic gate transitions.
3. Rapid retriggering on one channel.
4. Trigger pulses shorter than the network update interval.
5. Lost gate-on packets.
6. Lost gate-off packets.
7. Duplicate trigger packets.
8. Out-of-order event delivery.
9. Delayed state-refresh packets.
10. Publisher termination while gates are active.
11. Receiver startup while gates are already active.
12. Publisher restart with a new session ID.
13. Polyphonic channel-count changes.
14. Network interruption and reconnection.
15. Trigger queue overflow.
16. Different Rack audio sample rates on sender and receiver.
17. Concurrent streams from multiple publishers.
18. Clock drift between computers.
19. Wi-Fi multicast packet loss.
20. Network-worker overload and bounded queue recovery.

Tests should verify both the correctness of logical event handling and the absence of blocking operations in Rack's audio thread.

### 6.7.16 Acceptance criteria

The v0.1 gate and trigger subsystem is considered complete when:

- It supports 16 independent polyphonic channels.
- It preserves transitions occurring between periodic CV snapshots when their event packets are delivered.
- It generates configurable trigger pulses using the receiver's audio sample clock.
- It suppresses duplicate trigger events.
- It reconciles gate state following packet loss.
- It releases active gates after configured stream or publisher timeouts.
- It safely handles publisher restarts and session changes.
- It exposes meaningful network and event diagnostics.
- It operates without socket I/O, blocking locks, or dynamic allocation in Rack's audio callback.
- It degrades predictably when network conditions exceed the configured buffering and reliability limits.

The protocol explicitly does not guarantee lossless event delivery or sample-accurate timing across independent computers in v0.1.