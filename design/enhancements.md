# VCV Network Bus — Enhancements

**Version:** 0.1  
**Status:** Proposed roadmap  
**Components:** vDNS (discovery/naming) and vCVB (topic-based CV and event transport)

## Purpose and baseline

This document catalogs enhancements beyond the initial VCV Rack networking module. The v0.1 baseline is assumed to provide named nodes and services, multicast discovery and heartbeats, configurable presence TTL and inactive-host tracking, exact topic subscriptions, UDP polyphonic CV transmission, gate/trigger events and state refresh, and basic configuration and diagnostics. Priorities are **P1** (near-term), **P2** (mid-term), and **P3** (exploratory). All features must keep socket operations and blocking synchronization off Rack's audio thread.

## 1. vDNS discovery and naming

| ID | Priority | Enhancement | Expected behavior |
|---|---|---|---|
| DNS-01 | P1 | Hierarchical browser | Browse hosts, Rack instances, services, and advertised topics live. |
| DNS-02 | P1 | Name conflict handling | Display conflicting aliases; route by stable node UUID, never ambiguous names. |
| DNS-03 | P1 | Service capability negotiation | Advertise supported versions, stream types, channel counts, and rates. |
| DNS-04 | P1 | Interface selection | Bind discovery and transport to an explicit network interface. |
| DNS-05 | P2 | IPv6 multicast | Support IPv6 endpoints and multicast scopes alongside IPv4. |
| DNS-06 | P2 | Favorites and cached peers | Preserve offline peers while clearly distinguishing cached from active records. |
| DNS-07 | P2 | Discovery domains | Isolate multiple studios or performances on one LAN. |
| DNS-08 | P2 | Service queries | Query specific service types without broadcasting full catalogs repeatedly. |
| DNS-09 | P3 | Cross-subnet discovery gateway | Relay approved discovery records across routed networks with loop prevention. |
| DNS-10 | P3 | DNS-SD/mDNS bridge | Optionally expose vDNS services to conventional local service discovery. |

## 2. vCVB topics and routing

| ID | Priority | Enhancement | Expected behavior |
|---|---|---|---|
| BUS-01 | P1 | Topic remapping | Map remote topic names to local output ports and save mappings in patches. |
| BUS-02 | P1 | Unicast fallback | Deliver CV directly when multicast is unavailable or undesirable. |
| BUS-03 | P1 | Reconstruction modes | Select hold, linear interpolation, or slew limiting per stream. |
| BUS-04 | P2 | Wildcard subscriptions | Support documented patterns such as `music/*/pitch` and `studio/**`. |
| BUS-05 | P2 | Multiple-publisher policies | Select source, priority, sum, max, or last-writer explicitly. |
| BUS-06 | P2 | Sparse/delta encoding | Send changed channels plus periodic authoritative full snapshots. |
| BUS-07 | P2 | Adaptive publish rate | Reduce bandwidth for steady signals while preserving event transitions. |
| BUS-08 | P2 | QoS classes | Separate continuous CV, gate events, clocks, and reliable commands. |
| BUS-09 | P2 | Reliable control plane | Acknowledged, idempotency-aware unicast commands for critical actions. |
| BUS-10 | P3 | Topic bridges | Relay selected topics between networks with hop limits and loop detection. |

## 3. Musical events and synchronization

| ID | Priority | Enhancement | Expected behavior |
|---|---|---|---|
| MUS-01 | P1 | Clock-offset estimates | Show offset uncertainty and measured jitter for each sender. |
| MUS-02 | P2 | Transport messages | Carry tempo, meter, beat position, start, stop, continue, and reset. |
| MUS-03 | P2 | Scheduled actions | Execute an event at a specified future timestamp, beat, or bar. |
| MUS-04 | P2 | Structured note events | Send note IDs, pitch, velocity, onset, release, and channel ownership. |
| MUS-05 | P2 | Per-note expression | Carry named timbre controls and automation trajectories per note. |
| MUS-06 | P2 | Shared expression curves | Transfer curve definitions reliably and reference them by immutable IDs. |
| MUS-07 | P2 | Voice allocation metadata | Coordinate note-to-channel assignment, release, and voice stealing. |
| MUS-08 | P2 | PTP synchronization | Use PTP where available, retaining a documented fallback. |
| MUS-09 | P3 | Clock leader election | Detect and resolve conflicting transport leaders. |
| MUS-10 | P3 | MIDI/MPE/MIDI 2.0 bridge | Translate supported expression data, reporting lossy mappings. |
| MUS-11 | P3 | Distributed pattern transport | Publish versioned clips, patterns, and arrangement changes. |

## 4. Gate, trigger, and failure handling

| ID | Priority | Enhancement | Expected behavior |
|---|---|---|---|
| EVT-01 | P1 | Per-stream failsafe policy | Choose release gates, zero CV, hold, or slew to safe value on timeout. |
| EVT-02 | P1 | Event diagnostics | Count missing, duplicated, reordered, late, and discarded events. |
| EVT-03 | P1 | Loss/reordering simulator | Test recovery under configurable delay, duplication, and packet loss. |
| EVT-04 | P2 | Redundant critical events | Optionally repeat gate-off/trigger events with duplicate suppression. |
| EVT-05 | P2 | Retrigger policies | Queue, restart, or ignore overlapping trigger pulses. |
| EVT-06 | P2 | Stream health negotiation | Agree on heartbeat/state-refresh and timeout expectations. |
| EVT-07 | P3 | Selective event acknowledgments | Confirm critical event delivery over a reliable side channel. |

## 5. Rack interface and workflow

| ID | Priority | Enhancement | Expected behavior |
|---|---|---|---|
| UI-01 | P1 | Network browser module | Select a discovered service/topic without entering an IP address. |
| UI-02 | P1 | Topic autocomplete and validation | Suggest advertised topics and reject invalid identifiers. |
| UI-03 | P1 | Health indicators | Show active, suspect, inactive, jitter, loss, and stream timeouts. |
| UI-04 | P2 | CV port expanders | Add independently mapped polyphonic CV input/output banks. |
| UI-05 | P2 | Visual routing matrix | Connect local ports to remote topics with drag-and-drop mapping. |
| UI-06 | P2 | Saved network presets | Recall topic and interface settings without cloning node UUIDs. |
| UI-07 | P2 | Traffic inspector | Inspect recent message headers and values with bounded logging. |
| UI-08 | P3 | Remote control dashboard | Monitor and manage authorized peers from an external interface. |

## 6. Security and administration

| ID | Priority | Enhancement | Expected behavior |
|---|---|---|---|
| SEC-01 | P1 | Interface and peer filtering | Restrict incoming traffic to configured interfaces/endpoints. |
| SEC-02 | P1 | Strict packet validation | Enforce version, size, channel count, finite floats, and bounded queues. |
| SEC-03 | P2 | Authenticated node identities | Prevent spoofed identities and unauthorized control messages. |
| SEC-04 | P2 | Topic permissions | Allow publishing, subscribing, or control by authenticated peer/group. |
| SEC-05 | P2 | Encrypted unicast control | Protect sensitive control messages on untrusted networks. |
| SEC-06 | P3 | Secure network gateway | Connect trusted remote sites with explicit routing and access policies. |

## 7. Interoperability and distributed synthesis

| ID | Priority | Enhancement | Expected behavior |
|---|---|---|---|
| INT-01 | P2 | OSC bridge | Map vCVB topics to OSC addresses and numeric/event payloads. |
| INT-02 | P2 | Public C/C++ SDK | Enable standalone publishers and subscribers using the same protocol. |
| INT-03 | P2 | Capture/replay | Record timestamped traffic and reproduce it for testing or performance. |
| INT-04 | P3 | Python/JavaScript clients | Control and observe the network from external generative tools. |
| INT-05 | P3 | Distributed scene orchestration | Coordinate named patch states and transitions with partial-failure reporting. |
| INT-06 | P3 | Authorized remote parameters | Map approved topic commands to Rack parameters. |
| INT-07 | P3 | Network audio transport | Explore a separate, synchronized audio path with its own latency budget. |
| INT-08 | P3 | Distributed synthesis nodes | Separate sequencer, modulation, synthesis, and effects across computers. |

## 8. Recommended release roadmap

| Release | Focus | Candidate items |
|---|---|---|
| v0.2 | Robust first-use experience | DNS-01–04, BUS-01–03, EVT-01–03, UI-01–03, SEC-01–02 |
| v0.3 | Richer routing | DNS-06–08, BUS-04–09, UI-04–07 |
| v0.4 | Musical coordination | MUS-01–04, MUS-07, INT-01–03 |
| v0.5 | Expressive sequencing | MUS-05–06, MUS-08, MUS-10 |
| v1.0 candidate | Interoperability and hardening | DNS-05, SEC-03–05, protocol conformance and cross-platform testing |
| Later | Distributed studio | DNS-09–10, MUS-09/11, INT-04–08 |

The roadmap is illustrative, not a release commitment. Security work must move earlier if use expands beyond a trusted LAN.

## 9. Cross-cutting engineering requirements

1. **Real-time safety:** Network work occurs outside Rack's audio callback; handoff uses bounded preallocated queues or safe immutable snapshots.
2. **Versioned wire format:** Define byte order, float encoding, length limits, and extensibility for every packet.
3. **Stable identity:** Separate persistent node ID, runtime session ID, human name, and physical endpoint.
4. **Safe defaults:** Multicast scope remains local by default; remote control is opt-in.
5. **Failure recovery:** Lost gate-off events must eventually reconcile through snapshots and timeout policies.
6. **Deterministic routing:** Ambiguous names and multiple publishers require explicit resolution policies.
7. **Observability:** Measure latency, jitter, packet loss, queue overflow, and timeout events.
8. **Resource bounds:** Cap topics, payload sizes, queue depths, and retained peer records.
9. **Clock separation:** Keep sender monotonic time, receiver monotonic time, and musical beat position distinct.
10. **Compatibility:** Provide independent protocol fixtures and tests on macOS, Windows, and Linux.

## 10. Open decisions

- Should one Rack process be one vDNS node with multiple module services? **Proposed:** yes.
- Should multicast CV use the same group as discovery? **Proposed:** separate configurable groups.
- Should a topic have multiple publishers? **Proposed:** yes, but require an explicit receiver arbitration policy.
- How is a service's advertised TTL bounded? **Proposed:** receiver applies its configured maximum.
- How should remote command authorization work? **Proposed:** no arbitrary command execution; require approved commands and authenticated peers in later versions.
- How are missed triggers handled? **Proposed:** best-effort with optional redundancy; do not claim exactly-once delivery.
- What timing guarantees are realistic? **Proposed:** expose measured uncertainty and use buffering, not sample-accuracy claims.

## 11. Next documents

- `protocol.md` — precise packet schemas, state machines, topic grammar, and compatibility rules.
- `architecture.md` — Rack modules, network manager, threading, queues, and lifecycle.
- `test-plan.md` — interoperability, timing, failure injection, and real-time safety tests.
- `milestones.md` — development stages and release acceptance criteria.
