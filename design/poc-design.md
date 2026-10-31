# VCV Network Bus — Proof-of-Concept Design

**Status:** Implementation specification  
**Version:** 0.1 POC  
**Date:** October 31, 2026  
**Target:** VCV Rack 2 plugin, C++  
**Default deliverable:** macOS  
**Current deliverable:** macOS only  
**Future deliverables:** Linux and Windows  
**Primary transport:** IPv4 UDP multicast on a local network  
**Secondary transport:** UDP unicast for explicitly selected peers and control requests

## 1. Purpose

This document is the implementation contract for a proof of concept (POC) of the
VCV Network Bus. It is written for an AI coding assistant or engineering team
that will implement the first working vertical slice from an otherwise empty
plugin repository.

The POC must demonstrate that independent VCV Rack processes can:

1. Discover one another without manually entering IP addresses.
2. Advertise named services and exact topics.
3. Publish and subscribe to monophonic and polyphonic CV.
4. Preserve gate and trigger events as distinct events rather than relying only
   on periodic CV snapshots.
5. Recover predictably from packet loss, duplication, reordering, and process
   restart.
6. Keep all socket I/O, parsing, allocation, and blocking synchronization off
   Rack's real-time audio thread.
7. Expose enough diagnostics to explain whether a failure is discovery,
   transport, scheduling, or application configuration.

The POC is intentionally narrower than a production network protocol. It must
establish a reliable architecture and measurable behavior without prematurely
implementing audio streaming, cross-subnet routing, authentication, or a full
configuration browser.

## 2. Source requirements and design authority

The implementation must preserve the intent of:

- `initial-design.md`: architecture, vDNS behavior, vCVB packet semantics,
  gate/trigger handling, failure policy, and real-time constraints.
- `use-cases.md`: core discovery, routing, CV, musical, operational, and
  developer scenarios.
- `enhancements.md`: post-POC roadmap and explicit future features.

When this document conflicts with a source document, use the following order:

1. Safety and real-time constraints.
2. Explicit acceptance criteria in this document.
3. Core use cases.
4. Initial design.
5. Enhancements and future roadmap.

Do not silently expand the POC to implement a future feature. Record such a
proposal separately and preserve the bounded scope below.

## 3. POC scope

### 3.1 Included

- One process-wide `NetworkManager` per Rack process.
- One vDNS node identity per Rack process.
- Persistent `nodeId`, runtime `sessionId`.
- Configurable node name and network interface.
- IPv4 multicast discovery and CV/event transport.
- Heartbeats, startup queries, announcements, service updates, and goodbye.
- Peer registry with `ACTIVE`, `SUSPECT`, `INACTIVE`, and `CONFLICT` states.
- Exact, case-sensitive topic matching.
- Explicit publisher selection when several publishers advertise one topic.
- CV streams with 1–16 channels and bounded configurable update rates.
- `CV_STATE`, `GATE_ON`, `GATE_OFF`, `TRIGGER`, and `GATE_STATE`.
- Sequence numbers, event IDs, duplicate suppression, session fencing, and
  periodic gate-state refresh.
- Bounded jitter buffering and receiver-side trigger scheduling.
- Configurable stream timeout and fail-safe output behavior.
- Diagnostics counters and a test-only impairment layer.
- Patch persistence for stable configuration, but not live peer status.
- Unit tests that do not require Rack or a live network.
- Integration tests using loopback or a local multicast-capable interface.

### 3.2 Platform deliverables

The current POC implementation and validation target is macOS only. Linux and
Windows remain planned deliverables and must not be treated as current
milestone gates.

| Platform | Role | Required output |
|---|---|---|
| macOS | Current development and validation platform | Buildable VCV Rack plugin artifact, unit tests, and live two-process multicast validation where available |
| Linux | Future deliverable | Portable code should avoid unnecessary blockers, but Linux build and live validation are deferred |
| Windows | Future deliverable | Portable code should avoid unnecessary blockers, but Windows build and live validation are deferred |

The default workflow is macOS. Shared protocol, registry, routing, and event
logic should remain portable, but do not delay current macOS milestones to
establish Linux or Windows toolchains.

Platform-dependent code must be isolated behind narrow adapters for:

- UDP socket handles and close/error behavior;
- multicast join/leave and interface selection;
- interface enumeration and local-address discovery;
- monotonic clocks and sleep/wake primitives;
- worker-thread startup and shutdown;
- path separators and patch/configuration locations.

Use the platform conventions and compiler settings already required by the VCV
Rack SDK. Do not invent separate protocol implementations per operating
system. A platform-specific limitation is an acceptance finding that must be
documented, not a reason to silently omit the deliverable.

### 3.3 Explicitly excluded

- Audio transport or audio-rate CV.
- IPv6.
- Automatic multicast routing across subnets.
- Internet discovery, NAT traversal, or firewall configuration.
- Encryption, authenticated identities, and authorization.
- Arbitrary remote code execution or arbitrary Rack parameter mutation.
- Wildcard topics, aliases, transforms, topic bridges, and multiple-publisher
  mixing policies.
- Guaranteed delivery or exactly-once semantics.
- Sample-accurate synchronization across hosts.
- OSC, MIDI, standalone SDK, capture/replay files, and external dashboards.
- Automatic patch replication.

Future work may use the extension points defined here but must not weaken the
POC's validation and bounds.

## 4. Success criteria

The POC is successful only when all of the following are demonstrated:

1. Two Rack processes on one multicast-capable LAN discover each other within
   5 seconds under normal conditions.
2. A receiver can select a discovered publisher and exact topic without
   entering an IP address.
3. A changing 16-channel CV stream preserves channel count, order, and finite
   voltage values.
4. A trigger shorter than the CV publication period produces a local pulse
   when its event arrives within the configured late-event tolerance.
5. Duplicate trigger datagrams produce no duplicate pulse.
6. Dropped gate-off traffic is repaired by a state refresh or stream timeout.
7. Datagrams from a previous `sessionId` cannot change state after a publisher
   restart.
8. A peer transitions through active, suspect, and inactive states using local
   monotonic time; CV traffic does not renew vDNS presence.
9. A network interruption releases active gates and clears pending triggers by
   default.
10. No `process()` callback performs socket I/O, waits on a mutex, allocates,
    parses unbounded data, or calls UI code.
11. Malformed packets are rejected without process termination or state change.
12. The macOS build passes the unit-test suite; live multicast integration is
    run where the environment supports it.
13. The macOS artifact is buildable and manually validated as the current
    deliverable.
14. Platform-specific macOS socket, interface-selection, shutdown, and
    persistence behavior is either covered by automated tests or documented
    with a manual acceptance result.

## 5. Proposed implementation layout

The exact repository layout may follow the plugin template already in use, but
the implementation should converge on these separable units:

```text
src/
  NetworkBus.hpp                 public plugin-facing declarations
  network/
    NetworkManager.hpp/.cpp      lifecycle, sockets, worker, queues
    DiscoveryManager.hpp/.cpp    vDNS registry and state machine
    BusManager.hpp/.cpp          subscriptions, routing, stream state
    Protocol.hpp/.cpp            packet schemas and bounded codec
    Topic.hpp/.cpp               validation and exact matching
    Clock.hpp                    monotonic clock abstraction
    Diagnostics.hpp/.cpp         counters and immutable snapshots
    Impairment.hpp/.cpp          test-only loss/reorder/duplicate injection
  modules/
    CvPublisher.cpp              CV and event capture
    CvSubscriber.cpp             CV reconstruction and output scheduling
    NetworkBrowser.cpp           minimal discovery/topic selection UI
  tests/
    protocol_tests.cpp
    discovery_tests.cpp
    routing_tests.cpp
    event_tests.cpp
    impairment_tests.cpp
```

Keep protocol and state-machine code independent from Rack headers so it can be
tested in a normal C++ test runner. Use existing project conventions if the
repository adds a different source tree.

## 6. Runtime architecture

### 6.1 Thread ownership

Use three logical execution domains:

| Domain | Responsibilities | Forbidden operations |
|---|---|---|
| Rack audio thread | Read inputs, write outputs, detect transitions, consume ready events | Socket calls, waits, heap allocation, UI access |
| Network worker | Socket polling, multicast membership, packet decode/encode, registry mutation, timers | Direct Rack port access, direct module lifetime assumptions |
| UI thread | Menus, browser, diagnostics display, configuration changes | Direct socket calls and unbounded registry mutation |

The worker owns mutable network state. Rack modules communicate with it through
bounded single-producer/single-consumer or multi-producer/single-consumer
queues, immutable snapshots, and explicit registration tokens.

### 6.2 Lifetime rules

- `NetworkManager` is created lazily when the first network module needs it and
  destroyed only after all registrations are removed.
- A module unregisters before its destructor completes.
- Worker-delivered messages contain an opaque registration token, not a raw
  pointer that can outlive a module.
- Queue overflow is observable through diagnostics; it is never silently
  converted into success.
- Shutdown wakes the worker, sends best-effort `GOODBYE`, closes sockets, then
  joins the worker outside the audio thread.

### 6.3 Configuration versus runtime state

Persist:

- `nodeId`
- node name and optional instance label
- selected interface
- multicast groups and ports
- topic bindings
- selected publisher identity (`nodeId`, service ID, topic)
- update rate and reconstruction settings
- trigger/gate thresholds and fail-safe policy

Do not persist as authoritative:

- IP addresses learned from discovery
- peer active/inactive state
- last-seen timestamps
- runtime sequence numbers or event deduplication windows
- queue contents

## 7. Protocol contract

### 7.1 Packet framing

All packets use a bounded versioned binary frame. Do not serialize C++ object
memory directly.

Every packet begins with:

```text
magic[4]       ASCII "VCVB"
version        uint8, initially 1
kind           uint8
flags          uint16
headerLength   uint16
payloadLength  uint16
nodeId         16 bytes
sessionId      16 bytes
sequence       uint64
```

Use network byte order for integer fields and IEEE-754 binary32 for voltages.
Reject packets when:

- magic or version is unsupported;
- declared lengths exceed the datagram or configured maximum;
- channel count is greater than 16;
- topic length is zero, over 255 bytes, or contains invalid characters;
- a voltage is NaN or infinite;
- a sequence, session, or identifier field is truncated.

The POC packet maximum is 1400 bytes to avoid ordinary Ethernet
fragmentation. A larger service catalog must be queried over unicast or split
into bounded records.

### 7.2 Packet kinds

```text
DNS_ANNOUNCE
DNS_QUERY
DNS_RESPONSE
DNS_SERVICE_UPDATE
DNS_GOODBYE
CV_STATE
GATE_EVENT
GATE_STATE
CONTROL_STATE_REQUEST
```

Control requests are limited to the defined state request. Never add a
generic command string or an eval-like payload.

### 7.3 Discovery records

An announcement contains:

- node and session identity;
- node name and host name;
- sender sequence;
- advertised presence TTL;
- service revision;
- one or more bounded service records.

A service record contains service ID, display name, type, UDP port, protocol
version, capabilities, and bounded topic names.

Default values:

| Setting | Default |
|---|---:|
| Discovery group | `239.192.0.10` |
| Discovery port | `47000` |
| Bus group | `239.192.0.11` |
| Bus port | `47001` |
| Heartbeat | 2 s |
| Presence TTL | 10 s |
| Suspect threshold | 60% of effective TTL |
| Inactive retention | 5 min |
| Multicast hop limit | 1 |

All values are configuration defaults, not protocol constants.

The receiver computes:

```text
effectiveTTL = min(advertisedTTL, receiverMaximumTTL)
```

Use a monotonic local clock for expiry. A received CV packet does not renew
presence.

### 7.4 CV state

A `CV_STATE` record contains stream ID, topic, publisher identity, sequence,
sender monotonic timestamp, channel count, flags, and up to 16 voltages.

The POC supports 20, 50, 100, 250, 500, and 1000 Hz publication settings.
The default is 500 Hz. The receiver supports:

- hold-last-value (default);
- linear interpolation with a bounded jitter buffer;
- slew-limited transition.

The implementation must document measured latency and jitter; it must not
claim sample accuracy.

### 7.5 Gate and trigger events

`GATE_EVENT` contains:

- stream ID;
- per-stream sequence number;
- unique event ID;
- sender monotonic timestamp;
- event type (`GATE_ON`, `GATE_OFF`, or `TRIGGER`);
- channel index;
- voltage;
- trigger duration where applicable.

`GATE_STATE` contains an authoritative state for all active channels and is
published every 100 ms by default. A receiver may send
`CONTROL_STATE_REQUEST` after subscribing or reconnecting.

Default event behavior:

- rising threshold: 2.0 V;
- falling threshold: 0.1 V;
- active output: 10.0 V;
- inactive output: 0.0 V;
- trigger duration: 1 ms;
- jitter buffer: 5 ms;
- late-event tolerance: 10 ms;
- stream timeout: 500 ms;
- fail-safe: release gates, clear pending triggers;
- retrigger policy: bounded queue.

Use hysteresis for gate detection. Preserve event order within a stream.
Suppress duplicate event IDs within a bounded, session-scoped window. A new
session clears all prior ordering and deduplication state.

## 8. State machines

### 8.1 Peer lifecycle

```text
UNKNOWN -> ACTIVE       valid announcement/response
ACTIVE -> SUSPECT       no heartbeat at warning threshold
SUSPECT -> INACTIVE     no heartbeat at effective TTL
INACTIVE -> ACTIVE      valid announcement for current/new session
any     -> CONFLICT     same human name, distinct node IDs
```

Do not delete an active record merely because a name is duplicated. Route by
stable node ID and require explicit selection.

### 8.2 Stream lifecycle

```text
UNBOUND -> WAITING      valid saved binding, no packets yet
WAITING -> ACTIVE       first valid state or CV frame
ACTIVE  -> DEGRADED     gaps, late events, or queue pressure
ACTIVE  -> TIMED_OUT    no valid stream packet for stream timeout
DEGRADED -> ACTIVE      healthy ordered traffic resumes
TIMED_OUT -> WAITING    publisher/session is rediscovered
```

On `TIMED_OUT`, release gates, clear trigger work, and apply the configured
continuous-CV policy. On a new session, discard all queued old-session work.

## 9. Rack-facing POC modules

Implement the smallest UI that proves the vertical slice:

### 9.1 CV Publisher

- One polyphonic CV input.
- Optional gate/trigger input mode.
- Topic text setting.
- Service display name.
- Update-rate selection.
- Status and packet counters.
- Context menu for thresholds and redundancy.

The audio callback copies current input values into a preallocated frame/event
queue. It only detects threshold transitions and enqueues compact records.

### 9.2 CV Subscriber

- One polyphonic CV output.
- Topic/publisher binding.
- Reconstruction mode.
- Gate/trigger output mode.
- Timeout and fail-safe configuration.
- Status, latency, loss, and timeout indicators.

The audio callback consumes already-decoded, time-eligible records and writes
outputs. It never parses datagrams.

### 9.3 Minimal Network Browser

The browser may be a context-menu panel for the POC. It must list:

- active/suspect/inactive peers;
- duplicate-name conflicts;
- services and advertised topics;
- selected publisher identity;
- local interface and multicast status.

Full drag-and-drop routing and saved network presets are future work.

## 10. Testing strategy

### 10.1 Unit tests

Test without Rack:

- topic validation and exact matching;
- bounded packet encode/decode and endian behavior;
- malformed/truncated/oversized packets;
- finite-voltage and channel-count validation;
- peer lifecycle and effective TTL;
- duplicate-name conflict behavior;
- session restart fencing;
- CV reconstruction;
- event ordering and deduplication;
- channel-count reduction and gate release;
- timeout and fail-safe policy;
- bounded queue overflow reporting.

### 10.2 Impairment tests

Provide a deterministic test transport that can inject:

- loss;
- duplication;
- reordering;
- fixed and variable delay;
- burst loss;
- malformed packets;
- worker queue pressure.

Use a seeded impairment configuration so failures are reproducible. Assertions
must verify final logical state, not just packet counts.

### 10.3 Integration tests

Run at least:

1. Two local nodes discover each other.
2. One publisher and two subscribers receive the same topic.
3. Sixteen-channel CV preserves order and count.
4. A short trigger survives a publication interval.
5. A dropped gate-off is repaired.
6. A publisher restart rejects old-session packets.
7. Presence expires independently from active CV traffic.
8. Interface/socket startup and shutdown do not leak workers.

Live multicast tests may be skipped only with an explicit environment reason;
unit and impairment tests must still run.

### 10.4 Real-time safety checks

- Build a test-only audio callback harness that fails if allocation, blocking,
  socket calls, or unbounded work are invoked.
- Review every function reachable from `process()`.
- Run sustained publication with queue pressure and verify bounded behavior.
- Treat sanitizer, thread-safety, and undefined-behavior failures as release
  blockers.

## 11. Milestones and implementation gates

Milestones are ordered. Do not begin a later milestone until its gate passes.
When a milestone's deliverables and gate are complete, mark it complete by
appending `(**COMPLETE**)` to its title line, separated by one space. For
example: `### M0 — Repository and build baseline (**COMPLETE**)`.

### M0 — Repository and build baseline (**COMPLETE**)

**Status:** Complete for the macOS-only POC target. Linux and Windows remain
deferred future deliverables.

**Deliverables**

- Confirm plugin build, test runner, formatting, and supported platforms.
- Confirm macOS as the current build target and establish its build/test
  commands using the VCV Rack SDK.
- Record Linux and Windows as deferred platform targets; do not make their
  toolchains or CI a current M0 dependency.
- Add the proposed source/test boundaries without networking behavior.
- Record how tests and a two-process manual run are executed.

**Current baseline record**

- VCV Rack SDK locations: keep architecture-specific SDK checkouts outside
  this plugin repository under `../LIBS/Rack-SDK-ARM64` and
  `../LIBS/Rack-SDK-X64`. Configure the matching plugin build with `RACK_DIR`.
- Minimal Apple Silicon Rack plugin build:
  `make RACK_DIR=../LIBS/Rack-SDK-ARM64`. This produces
  `plugin.dylib` using the sibling SDK and the M0 plugin entry point.
- Portable smoke test: `make -C tests test`.
- Direct portable test build:
  `c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror
  tests/smoke_test.cc -o tests/smoke_test`.
- Windows smoke test command is retained in `tests/README.md` for the future
  platform bring-up and is not a current M0 gate.
- Two-process multicast manual run: deferred until the discovery and transport
  modules exist. The eventual run must start two independent Rack processes,
  verify discovery, publish/subscribe behavior, and record the selected
  interface and multicast result on macOS.

**Gate**

- The macOS ARM64 plugin build remains green with
  `make RACK_DIR=../LIBS/Rack-SDK-ARM64`. The x86_64 build uses the matching
  `Rack-SDK-X64` checkout and does not block the current ARM64 gate.
- Linux and Windows are explicitly deferred and do not block M0.
- A minimal test executable can run in CI or the repository's standard test
  workflow.

### M1 — Pure protocol and topic foundation (**COMPLETE**)

**Status:** Complete for the macOS-only POC target. Protocol code is portable
and compiled with the C++11 mode selected by the Rack SDK.

**Deliverables**

- Topic validator and exact matcher.
- Packet header, enums, bounded reader/writer, and codecs.
- Validation errors with explicit diagnostic codes.
- Unit tests for valid, truncated, oversized, malformed, and non-finite data.

**Gate**

- Round-trip fixtures are stable and independent of host endianness.
- No codec accepts data outside configured bounds.

### M2 — Network worker and vDNS registry (**COMPLETE**)

**Deliverables**

- Reusable UDP socket abstraction.
- Multicast join/leave and selected-interface binding.
- Worker lifecycle and shutdown.
- Announce/query/response/goodbye handling.
- Peer registry, TTL state machine, conflicts, service revision updates.
- Immutable registry snapshots for UI.

**Gate**

- Two processes discover each other.
- Heartbeat suppression produces active → suspect → inactive.
- CV-like traffic does not extend presence.
- Restart changes session ID while retaining node ID.

### M3 — CV publication and subscription (**COMPLETE**)

**Deliverables**

- Stream registration and exact topic routing.
- Bounded CV frame queues.
- Multichannel state publication and reconstruction.
- Publisher selection by node/session/service identity.
- Minimal publisher/subscriber modules.

**Gate**

- One publisher drives two subscribers.
- Sixteen channels preserve ordering and finite values.
- Queue overflow and malformed traffic are visible in diagnostics.

### M4 — Gate, trigger, and failure behavior (**COMPLETE**)

**Deliverables**

- Hysteretic transition detection.
- Gate events, trigger events, state refresh, and state request.
- Deduplication, sequence checks, jitter buffer, late policy.
- Stream timeout, session fencing, channel removal, and fail-safe outputs.

**Gate**

- All required event and impairment tests pass.
- Lost gate-off does not leave a gate indefinitely active.
- Duplicate triggers do not duplicate pulses.
- Old-session events cannot affect a restarted stream.

### M5 — Minimal UI, persistence, and diagnostics (**COMPLETE**)

**Deliverables**

- Browser/context menu for peers, services, topics, and publisher selection.
- Patch persistence for stable configuration.
- Status indicators and bounded counters.
- Explicit errors for unavailable interfaces, invalid bindings, and queue
  overflow.

**Gate**

- Save/reload preserves configuration but rebuilds discovery state.
- Duplicate names are visible and cannot be resolved silently.
- A user can complete the zero-configuration named-routing scenario.

### M6 — Cross-platform hardening and POC acceptance

**Deliverables**

- Cross-platform socket and timing fixes.
- macOS current-artifact validation; Linux and Windows bring-up is deferred.
- Platform matrix covering compiler, architecture, SDK/toolchain, plugin
  output location, interface selection, multicast join/leave, and clean
  shutdown.
- Sanitizer/thread-safety runs where available.
- Manual acceptance report for macOS. Windows and Linux reports are deferred.
- Developer documentation for building, testing, and running two nodes.
- Known limitations and measured latency/loss results.

**Gate**

- All success criteria in section 4 are evidenced by tests or a documented
  manual result.
- macOS is validated as the current deliverable.
- Linux and Windows remain documented future targets and are not release gates
  until the project explicitly reopens their implementation scope.
- No known audio-thread safety violation remains.
- POC is tagged internally as ready for review, not as production-ready.

## 12. AI coding assistant instructions

The coding assistant implementing this document must:

1. Inspect the repository's existing build and plugin conventions before adding
   files. Reuse its dependency and test infrastructure.
2. Implement one milestone at a time and run that milestone's gate before
   continuing.
3. Prefer small, reviewable commits or changes grouped by milestone.
4. When a milestone's deliverables and gate pass, append `(**COMPLETE**)` to
   that milestone's title line in this document.
5. Before implementing, ask the user for clarification about any unresolved
    question, requirement, ambiguity, or design decision that could affect
    behavior, scope, compatibility, or acceptance. Do not silently guess.
6. Keep protocol code deterministic and dependency-light.
7. Write clean, maintainable C++ in accordance with established clean-code
    conventions and SOLID principles. Prefer cohesive classes, explicit
    interfaces, dependency inversion at platform boundaries, descriptive names,
    small functions, single-purpose modules, and minimal duplication. Within
    the limits imposed by VCV Rack module requirements, make code only as
    complex as needed; avoid speculative abstractions, unnecessary indirection,
    and premature generalization.
8. Use fixed-width integer types, explicit serialization, bounded containers,
    and checked length arithmetic.
9. Keep all errors observable through typed results or repository-standard
    logging. Do not use broad catches, silent fallback success, or ignored
    queue failures.
10. Never add socket calls, mutex waits, heap allocation, logging with unknown
    allocation behavior, or UI calls to the Rack audio callback.
11. Never trust a packet's node name as identity; use node ID and session ID.
12. Never execute arbitrary incoming command text. Add only enumerated,
    validated message types.
13. Add tests alongside each state machine or codec change, including negative
    cases and impairment behavior.
14. Preserve unrelated work in a dirty worktree. Stop and ask only when an
    existing change conflicts with the requested implementation.
15. Do not implement future roadmap items as hidden prerequisites. If an
    extension point is needed, add the smallest explicit interface and test it.
16. Report the exact commands run, tests passed, skipped environmental tests,
    and any measured timing or packet-loss limitations.
17. Treat macOS as the current development and release-validation platform;
    Linux and Windows are deferred future targets.
18. Do not introduce POSIX-only or Windows-only APIs into shared code. Isolate
    socket handles, multicast membership, interface enumeration, timing, thread
    startup, and filesystem paths behind the platform adapters.
19. When a platform cannot run live multicast in the current environment,
    continue running portable protocol/state tests and record the skipped
    integration condition explicitly; do not mark the platform unsupported.

## 13. Review checklist

Before calling the POC complete, reviewers must be able to answer “yes” to all
of these:

- Are node ID, session ID, display name, and endpoint distinct?
- Are all packet lengths and values validated before state mutation?
- Are registry and stream state owned outside the audio thread?
- Are queues bounded and overflow observable?
- Are publisher restarts fenced against stale packets?
- Are gates released on stream timeout and publisher loss?
- Are trigger duplicates suppressed?
- Are exact-topic and explicit-publisher rules enforced?
- Are presence TTL and stream timeout measured independently?
- Is every persisted field safe to reload when the peer is offline?
- Do tests cover loss, duplication, reordering, malformed data, and restart?
- Are multicast limitations and timing uncertainty visible to users?

## 14. Post-POC roadmap

Only after M6 passes should the project consider:

- P1: hierarchical browser improvements, interface filtering, capability
  negotiation, unicast fallback, reconstruction tuning, topic remapping,
  stream health diagnostics, and a loss/reordering simulator UI.
- P2: wildcard topics, QoS classes, reliable control, transport messages,
  clock-offset estimates, scheduled actions, OSC/MIDI bridges, capture/replay,
  authenticated identities, and encrypted unicast control.
- P3: IPv6, cross-subnet gateways, topic bridges, PTP, distributed pattern
  transport, external clients, and network audio.

Every roadmap item must retain the POC invariants: bounded resources,
explicit validation, no arbitrary commands, and no blocking work in the audio
thread.
