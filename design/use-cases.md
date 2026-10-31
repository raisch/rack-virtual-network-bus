# VCV Network Bus — Use Cases

**Version:** 0.1  
**Components:** vDNS (discovery and naming), vCVB (topic-based UDP CV bus)  
**Target:** VCV Rack 2, local-area networking

## Scope and conventions

**Core** means targeted for the initial implementation; **Future** means planned expansion. A node is a logical Rack networking participant; several nodes may exist on one computer. vDNS names are application-local aliases, not operating-system DNS records. vCVB uses best-effort UDP, so no use case assumes guaranteed delivery or sample-accurate synchronization. Topics are hierarchical strings such as `music/lead/gate`; v0.1 supports exact matching. CV streams carry up to 16 polyphonic channels.

## 1. Discovery and naming (vDNS)

### UC-D01 — Automatic peer discovery [Core]
**Actor:** Rack operator. **Scenario:** Start Rack on two computers connected to the same multicast-capable LAN. **Expected:** Each instance sends a discovery query and periodic presence announcements; both appear in the other's host list without manually entering IP addresses.

### UC-D02 — Resolve a named service [Core]
**Scenario:** A receiver selects `studio-main / sequencer`. **Expected:** vDNS returns the service's current IP endpoint(s), UDP port, protocol version, and capabilities. Ambiguous names require explicit selection.

### UC-D03 — Advertise multiple services [Core]
**Scenario:** One node exposes clock, sequencer, and modulation services. **Expected:** Each service has its own identifier, type, endpoints, and advertised topics.

### UC-D04 — Heartbeat and TTL expiration [Core]
**Scenario:** A host stops announcing. **Expected:** It transitions from active to suspect and then inactive after its effective presence TTL. The receiver measures TTL using its own monotonic clock; vCVB traffic alone does not renew vDNS presence.

### UC-D05 — Recovery after restart [Core]
**Scenario:** A Rack process crashes and restarts. **Expected:** Its persistent node ID remains the same but its session ID changes. Peers discard stale session-specific state and recognize the new incarnation.

### UC-D06 — Graceful shutdown [Core]
**Scenario:** A node exits normally. **Expected:** A best-effort `GOODBYE` immediately invalidates that session; TTL expiration still handles crashes and packet loss.

### UC-D07 — Duplicate human-readable names [Core]
**Scenario:** Two nodes use `studio-main`. **Expected:** Both remain discoverable and are distinguished by UUID, host, and endpoint; no arbitrary resolution occurs.

### UC-D08 — Multiple Rack instances per computer [Core]
**Scenario:** Two Rack processes run on one machine. **Expected:** Each has an independent node identity and services, and both can participate on the same discovery multicast port.

### UC-D09 — Service changes during a session [Core]
**Scenario:** A module changes its name or advertised topics. **Expected:** Its service revision changes and peers refresh the registry without restarting Rack.

### UC-D10 — Network-interface selection [Core]
**Scenario:** A machine has Ethernet, Wi-Fi, and VPN adapters. **Expected:** The operator selects the discovery interface; the UI shows the effective local address and multicast status.

### UC-D11 — Browse services by capability [Core]
**Scenario:** A receiver needs a clock source. **Expected:** It filters active services by type, such as `vcvb.clock`, and shows matching publishers.

### UC-D12 — Preserve offline favorites [Future]
**Scenario:** A familiar node remains offline longer than the registry retention period. **Expected:** A pinned entry remains visible as offline without being treated as active.

### UC-D13 — Discovery across subnets [Future]
**Scenario:** Nodes occupy different VLANs. **Expected:** Explicit unicast peers, multicast routing, or a discovery relay bridge networks; the default multicast hop limit remains local.

## 2. Topic-addressed messaging (vCVB)

### UC-T01 — Publish and subscribe [Core]
**Scenario:** A publisher sends to `modulation/lfo1`; two receivers subscribe. **Expected:** Both receive the stream through multicast and independently reconstruct local outputs.

### UC-T02 — Topic-based remote reaction [Core]
**Scenario:** A node receives a typed message on `control/scene/activate`. **Expected:** It maps the message to a configured local CV value, trigger, or explicitly supported action. Incoming messages never execute arbitrary code.

### UC-T03 — Transport commands [Core]
**Scenario:** A master patch sends `transport/start`, `transport/stop`, and `transport/reset`. **Expected:** Subscribed nodes emit corresponding local events subject to network latency and jitter.

### UC-T04 — Select a publisher when topics collide [Core]
**Scenario:** Two nodes publish `clock/main`. **Expected:** A subscriber binds to a specific publisher identity rather than silently combining streams.

### UC-T05 — Discover available topics [Core]
**Scenario:** An operator configures a receive module. **Expected:** A topic browser groups advertised topics by node and service and shows publisher availability.

### UC-T06 — Change topic bindings live [Core]
**Scenario:** A receiver switches from `modulation/lfo1` to `modulation/lfo2`. **Expected:** The subscription changes without blocking the Rack audio thread; missing topics are clearly identified.

### UC-T07 — Validate message types [Core]
**Scenario:** A control message is sent to a CV-only subscription. **Expected:** The incompatible payload is rejected and counted in diagnostics.

### UC-T08 — Hierarchical wildcard subscriptions [Future]
**Scenario:** Subscribe to `music/lead/*`. **Expected:** Defined wildcard rules match a family of topics, with bounded filtering cost.

### UC-T09 — Topic aliases and transforms [Future]
**Scenario:** A remote publisher calls a control `timbre/brightness` while a local patch expects `filter/cutoff`. **Expected:** A mapping rule translates the name and optionally scales its values.

### UC-T10 — Request/reply messages [Future]
**Scenario:** Ask a remote Rack instance for its selected preset. **Expected:** Correlated request/reply messages return typed responses or explicit timeouts.

## 3. CV, gate, and trigger transport

### UC-C01 — Monophonic CV across computers [Core]
**Scenario:** An LFO on computer A controls a filter on B. **Expected:** Sampled CV is transmitted at a configured update rate and reconstructed using hold or interpolation.

### UC-C02 — Polyphonic CV [Core]
**Scenario:** A sequencer publishes 16 pitch channels. **Expected:** The receiver preserves channel count, order, and finite voltage values.

### UC-C03 — Remote polyphonic instrument [Core]
**Scenario:** A sequencer publishes pitch, gate, velocity, and four timbre lanes. **Expected:** Matching channel indices across the streams correspond to the same allocated voice; the sender is responsible for consistent allocation.

### UC-C04 — Short triggers [Core]
**Scenario:** A 1 ms pulse advances a remote sequencer. **Expected:** The sender transmits a discrete trigger event, and the receiver reconstructs a local pulse if the event arrives within the accepted timing window. A periodic CV snapshot alone is insufficient.

### UC-C05 — Lost gate-off recovery [Core]
**Scenario:** A gate-off datagram is dropped. **Expected:** A later authoritative gate-state refresh or stream timeout releases the remote gate; immediate recovery is not guaranteed.

### UC-C06 — Duplicate trigger suppression [Core]
**Scenario:** A trigger packet arrives twice. **Expected:** Its event ID prevents duplicate execution within a bounded deduplication window.

### UC-C07 — Out-of-order event handling [Core]
**Scenario:** A gate-off arrives before its preceding gate-on. **Expected:** The receiver uses stream sequence numbers and a bounded jitter buffer to reject stale transitions and preserve valid ordering.

### UC-C08 — Polyphonic channel-count changes [Core]
**Scenario:** A source drops from eight channels to three. **Expected:** Removed channels are safely released before the output channel count changes.

### UC-C09 — Stream timeout distinct from host TTL [Core]
**Scenario:** A node remains discoverable, but one gate stream stops sending. **Expected:** The stream times out independently, and its receiver applies a safe gate-release policy.

### UC-C10 — Multiple independent subscribers [Core]
**Scenario:** One modulation stream controls ten remote Rack patches. **Expected:** Receivers subscribe independently without the publisher opening ten separate unicast sessions.

### UC-C11 — Fast modulation / audio-rate CV [Future]
**Scenario:** Transmit modulation at tens of kilohertz. **Expected:** A specialized high-rate transport and buffering strategy is designed; the initial 500 Hz control path does not claim audio-rate fidelity.

### UC-C12 — Explicit note identity [Future]
**Scenario:** A polyphonic note moves between allocated voices. **Expected:** An expressive note-event protocol carries note IDs, lifecycle, and timbre curves independently of fixed CV channel positions.

## 4. Musical and performance applications

### UC-M01 — Distributed modular synthesizer [Core]
One computer runs the sequencer, another runs oscillators and filters, and a third supplies modulation. Named topics route pitch, gate, velocity, and timbre CV between them. Audio return is outside v0.1.

### UC-M02 — Shared master clock [Core]
A master Rack patch publishes clock and reset events. Remote sequencers follow them with best-effort timing; local clock generation may be preferable between synchronization cues.

### UC-M03 — Remote performance transport [Core]
A performer starts, stops, and resets several patches with topic messages from one controller.

### UC-M04 — Scene and preset selection [Core]
A scene-number CV or typed control message causes configured receivers to switch their local scene selection. Arbitrary patch modification is not implied.

### UC-M05 — Shared generative modulation [Core]
A single random walk, envelope, or chaotic modulator influences several Rack systems through a shared topic.

### UC-M06 — Multi-room sound installation [Core]
Rack instances in separate rooms subscribe to control streams, allowing one operator to change behavior across the installation. Network control does not guarantee aligned acoustic output.

### UC-M07 — Distributed polyrhythms [Core]
Several autonomous sequencers share transport/reset cues but retain independent rhythmic divisions and local scheduling.

### UC-M08 — Performer cue system [Core]
A performer publishes `performance/cue/next`; remote modules emit cue triggers for local patches.

### UC-M09 — Cross-computer feedback modulation [Future]
A signal passes from A to B and back to A. Explicit delays, rate limits, and loop diagnostics prevent uncontrolled feedback traffic.

### UC-M10 — Per-note expressive performance [Future]
The network carries structured notes with pitch, velocity, duration, and per-note timbre trajectories; receiving machines render their own CV curves.

### UC-M11 — Distributed generative composition [Future]
Multiple sequencers exchange typed motifs, pattern changes, and harmonic constraints, rather than only sampled CV.

### UC-M12 — Interactive installations [Future]
An approved gateway converts sensors or audience interactions into rate-limited, validated vCVB topics.

## 5. Operations, safety, and diagnostics

### UC-O01 — Inspect network status [Core]
A module displays active, suspect, and inactive peers; last-seen time; service list; effective TTL; and local network interface.

### UC-O02 — Monitor stream quality [Core]
A diagnostics panel shows packet rates, dropped and malformed packets, sequence gaps, duplicates, late events, and gate timeouts.

### UC-O03 — Network interruption [Core]
On link loss, peers expire and streams apply configured fail-safe output behavior. On reconnection, fresh announcements and session identifiers restore operation without accepting stale queued events.

### UC-O04 — Multicast-hostile Wi-Fi [Core]
The UI indicates discovery and loss problems; manually configured unicast endpoints provide a fallback where supported.

### UC-O05 — Patch persistence [Core]
Rack saves module names, topic bindings, selected peer identities, timing settings, and output policies. Discovered peer status is rebuilt at runtime.

### UC-O06 — Real-time-safe audio processing [Core]
Socket I/O, packet parsing, discovery registry updates, and allocations run outside Rack's `process()` callback. Preallocated bounded queues carry samples/events across threads.

### UC-O07 — Malformed or hostile packets [Core]
Reject unsupported versions, invalid lengths, out-of-range channel counts, non-finite voltages, invalid topic strings, and stale sessions before changing state.

### UC-O08 — Safe module deletion [Core]
Deleting a sender or receiver unregisters its subscriptions without a worker thread accessing freed memory.

### UC-O09 — Gate fail-safe behavior [Core]
A stream timeout releases active gates and clears pending triggers by default; continuous CV can hold or zero according to configuration.

### UC-O10 — Discovery burst control [Core]
Startup query responses use randomized delays, bounded packet sizes, and configurable heartbeat cadence to avoid multicast storms.

### UC-O11 — Security boundaries [Core]
Network reception is explicitly enabled, interface-scoped, and restricted to validated message schemas. No unauthenticated message can run arbitrary OS commands.

### UC-O12 — Record and replay traces [Future]
Capture timestamped network messages to a diagnostic file and replay them to reproduce timing and packet-loss bugs.

### UC-O13 — Authentication and encryption [Future]
Trusted node pairing and authenticated messages prevent unapproved publishers from controlling a Rack patch; encrypted transport protects sensitive control traffic.

### UC-O14 — Cross-platform compatibility [Core]
Test macOS, Windows, and Linux Rack instances on the same LAN with different local audio sample rates.

## 6. Developer and integration use cases

### UC-I01 — Independent vDNS and vCVB implementation [Core]
A developer can unit-test registry logic, wire serialization, topic routing, and CV reconstruction without launching the full Rack UI.

### UC-I02 — Protocol version negotiation [Core]
A peer advertises supported versions/capabilities. Incompatible packets are rejected with diagnostics rather than interpreted incorrectly.

### UC-I03 — Third-party publisher [Future]
An external application emits validated vCVB messages that a Rack module receives without running Rack on the publisher computer.

### UC-I04 — Headless bridge service [Future]
A small standalone daemon bridges vDNS/vCVB to other networks or control protocols while respecting security and routing policies.

### UC-I05 — OSC and MIDI gateways [Future]
Gateway modules translate OSC or MIDI messages to typed vCVB topics and vice versa, with explicit semantics for timing and channel mapping.

### UC-I06 — Automated network impairment tests [Core]
A test harness injects loss, reordering, duplication, jitter, process restart, and malformed packets; assertions verify bounded queues, safe gate release, and no audio-thread blocking.

## 7. Recommended end-to-end acceptance scenarios

1. **Zero-configuration discovery:** Two Rack processes on one multicast-capable LAN discover each other within a bounded interval after startup.
2. **Named routing:** A receiver selects a named publisher and topic and reconstructs a changing monophonic CV stream.
3. **Polyphony:** A 16-channel pitch stream arrives with correct channel order and count.
4. **Remote note:** Pitch and gate streams drive a remote polyphonic instrument; release events clear the correct channels.
5. **Trigger preservation:** A pulse shorter than the CV snapshot interval is transmitted as a discrete event and reconstructed when delivered on time.
6. **Lost release:** Dropping a gate-off packet is repaired by state refresh or stream timeout.
7. **Host expiry:** Suppressing discovery announcements changes active to suspect to inactive according to TTL without falsely treating CV traffic as a heartbeat.
8. **Session restart:** Old-session datagrams cannot reactivate gates after publisher restart.
9. **Multiple subscribers:** Two receivers consume the same multicast topic without requiring separate publisher streams.
10. **Real-time safety:** Sustained networking activity and malformed packets do not cause blocking socket operations or allocations inside Rack's audio callback.

## 8. Future enhancement priorities

**Near-term:** wildcard topics; topic aliases; pinned peers; explicit unicast discovery; trace recording; richer diagnostics; service capability negotiation.

**Medium-term:** authenticated peers; request/reply control; OSC/MIDI bridges; reliable unicast for critical events; time synchronization; multi-publisher arbitration; network-aware patch templates.

**Long-term:** expressive note/event transport; distributed voice allocation; high-rate modulation; audio transport as a separate subsystem; cross-subnet discovery relays; collaborative network composition; distributed patch orchestration.

## 9. Open design questions

1. Should node identity belong to each module, each Rack process, or a user-defined logical group? The current proposal supports logical nodes managed by one process-wide network manager.
2. Should publishers choose their own heartbeat TTL, or should receivers enforce a local maximum? The current proposal uses the minimum of the advertised TTL and the receiver maximum.
3. How many CV ports should a vCVB module expose, and should topic assignment be per port or per module?
4. Should a typed control message directly invoke an approved module action, or only produce CV/gate outputs in v0.1?
5. How should same-topic publishers be presented and selected when one becomes inactive?
6. Should there be one shared multicast data group or multiple groups partitioned by topic/service?
7. What latency targets are acceptable for clock and gate use on wired Ethernet versus Wi-Fi?
8. Which network settings are global to Rack, and which belong to a patch or individual module?

---

**Design principle:** Discovery should be decentralized and forgiving; musical event handling should be explicit about loss and timing; Rack's audio processing must remain deterministic and nonblocking.
