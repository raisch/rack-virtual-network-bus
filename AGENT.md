# VCV Network Bus Agent Instructions

## Project mission

This repository contains the proof of concept for VCV Network Bus, a VCV Rack
2 C++ plugin that discovers Rack processes and transports topic-addressed
control voltage, gate, trigger, and control events over a local-area network.

Before changing code, read the applicable documents from `design/`:

1. `design/poc-design.md` — implementation scope, protocol contract, milestones, and
   acceptance gates.
2. `design/initial-design.md` — detailed architecture and behavioral decisions.
3. `design/use-cases.md` — required user and failure scenarios.
4. `design/enhancements.md` — future work that must not be pulled into the POC
   accidentally.
5. `design/coding-style.md` — the project's C++ style reference.

Treat `design/poc-design.md` as the implementation contract. Preserve existing
repository conventions when they are more specific than the examples in that
document.

The VCV Rack SDK is an external build dependency. Keep architecture-specific
SDKs outside this repository under `../LIBS`:

```text
vcv-rack/
  LIBS/
    Rack-SDK-ARM64/
    Rack-SDK-X64/
  virtual-network-bus/
```

The default Apple Silicon build uses `RACK_DIR=../LIBS/Rack-SDK-ARM64`.
The Intel build uses `RACK_DIR=../LIBS/Rack-SDK-X64`. Do not vendor the SDK,
commit it, or place it under `src/`, `tests/`, or `design/`.

## C++ style requirements

Follow `design/coding-style.md` as closely as feasible. These project rules
adapt its Google C++ conventions to VCV Rack and the POC's real-time and
cross-platform constraints:

- Use the C++ language version supported by the repository and VCV Rack SDK.
  Do not use newer language features or non-standard compiler extensions merely
  for convenience.
- Prefer self-contained headers. Include what the file uses; do not rely on
  transitive includes. Use project-standard header guards and keep declarations
  separate from definitions unless an inline or template definition must be
  visible.
- Follow the repository's file-extension convention. If no convention exists,
  use lowercase descriptive names, `.h` headers, `.cc` implementations, and
  `_test.cc` tests.
- Order includes consistently: the associated header first in an implementation
  file, then project headers, third-party headers, and standard-library headers
  according to the local formatter or existing project convention.
- Use `PascalCase` for types, `snake_case` for variables, parameters, and
  non-constant data members, and a trailing underscore for private class data
  members. Use `kPascalCase` for fixed constants and `PascalCase` for functions
  unless existing Rack APIs require another spelling.
- Choose descriptive names and minimize unfamiliar abbreviations. Do not
  shorten names by deleting internal letters. Keep names consistent with the
  vDNS/vCVB terminology.
- Format with two spaces, never tabs, and keep lines at or below 80 characters
  except where splitting would harm a URL, literal, include, or required
  generated text. Use the repository's formatter when one is configured.
- Prefer short, focused functions. If a function grows beyond roughly 40 lines,
  consider extracting cohesive helpers without obscuring the control flow or
  introducing unnecessary abstraction.
- Prefer return values over output parameters. Put input-only parameters before
  output parameters. Use `const` references for non-optional inputs and make
  optionality explicit rather than using nullable references.
- Avoid implicit conversions; mark single-argument constructors and conversion
  operators `explicit` unless an implicit conversion is clearly intentional.
  Use `const`, `constexpr`, and `noexcept` when they accurately express the
  contract, but do not add them merely to force optimization.
- Use RAII and make ownership explicit. Prefer values and `std::unique_ptr` for
  exclusive ownership; use `std::shared_ptr` only when shared lifetime is
  required and documented. Avoid raw owning pointers.
- Do not use C++ exceptions in project code. Return explicit error results,
  status values, or validated fallible-operation results appropriate to the
  surrounding API. Do not hide failures behind assertions or silent defaults.
- Use `int` for ordinary small integers and exact-width types such as
  `int32_t` or `uint64_t` when wire format, storage, or arithmetic width
  requires it. Avoid unsigned types merely to express non-negativity; use them
  for bit patterns, modular arithmetic, or protocol fields where appropriate.
- Use `float` or `double` as required by the Rack or wire contract; do not use
  `long double`. Validate floating-point network values before state mutation.
- Use `auto` only when it improves readability or safety. Do not use type
  deduction solely to avoid spelling an obvious type.
- Add comments that explain intent, invariants, ownership, synchronization, or
  non-obvious tradeoffs. Prefer self-documenting code over comments that repeat
  the implementation. Document non-obvious public classes and functions,
  especially thread-safety assumptions and real-time restrictions.
- Keep comments grammatically complete and use `TODO` comments only when they
  identify a concrete follow-up. Do not add author lines or copied legal text
  unless required by the repository's license policy.

## Scope and priorities

The current POC implementation and validation target is macOS only:

- **macOS:** default development and validation platform.

Linux and Windows are future deliverables. Keep shared code reasonably
portable, but do not make current milestones depend on Linux or Windows
toolchains, CI, or live validation.

The initial transport is IPv4 UDP multicast with optional explicitly selected
UDP unicast. The POC includes:

- vDNS discovery, heartbeats, service advertisement, TTL expiry, and conflicts;
- exact topic matching and explicit publisher selection;
- 1–16-channel CV state streams;
- gate-on, gate-off, trigger, and authoritative gate-state refresh events;
- sequence validation, duplicate suppression, session fencing, jitter buffering,
  stream timeouts, and fail-safe output behavior;
- bounded diagnostics, impairment testing, and patch persistence.

Do not add audio transport, audio-rate CV, IPv6, cross-subnet routing,
authentication, encryption, wildcard topics, arbitrary remote commands,
guaranteed delivery, or sample-accurate synchronization as part of this POC.
If a future feature affects the design, add the smallest explicit extension
point and document the proposal rather than implementing hidden scope.

## Required engineering practices

### Real-time safety

Rack's audio callback is a hard real-time boundary:

- Never perform socket I/O, packet parsing, blocking waits, mutex acquisition,
  heap allocation, filesystem access, UI calls, or unbounded loops from
  `process()`.
- Read inputs, detect threshold transitions, copy bounded values, and consume
  already-prepared output events only in the audio thread.
- Use preallocated bounded queues or immutable snapshots between the audio
  thread, network worker, and UI thread.
- Make queue overflow, dropped frames, and discarded events observable through
  diagnostics. Never silently report success.
- Review every function reachable from `process()` for indirect allocation,
  logging, locking, and platform calls.

### Thread and lifetime ownership

- Use one process-wide `NetworkManager` for all networking modules.
- Keep mutable socket, registry, routing, and stream state on the network
  worker.
- Register modules with opaque tokens or handles; never retain raw module
  pointers in asynchronous work after unregistering.
- Unregister before a module destructor completes.
- Stop the worker and join it outside the audio thread.
- Send `GOODBYE` best effort, but always rely on TTL and stream timeout for
  crash or network-loss recovery.

### Protocol and validation

- Serialize explicit wire fields; never transmit C++ object memory.
- Use fixed-width integer types, explicit byte order, bounded length arithmetic,
  and finite IEEE-754 binary32 voltage values.
- Reject unsupported versions, invalid magic, truncated or oversized payloads,
  invalid topics, invalid channel counts, non-finite voltages, and stale
  sessions before mutating state.
- Keep the packet limit at or below the POC's 1400-byte target unless the
  protocol design is updated deliberately.
- Treat `nodeId` as stable identity and `sessionId` as runtime incarnation.
  Human-readable names are aliases and may conflict.
- Never resolve an ambiguous name implicitly.
- Never execute arbitrary incoming command text; accept only enumerated,
  validated message kinds.

### Discovery and stream behavior

- Measure vDNS presence TTL and vCVB stream timeout independently using local
  monotonic time.
- Ordinary CV traffic must not renew vDNS presence.
- Fence all queued events and frames by publisher session.
- Preserve event ordering per stream, suppress duplicate event IDs, and handle
  missing/reordered traffic explicitly.
- Release active gates and clear pending triggers on stream timeout or publisher
  loss by default.
- Release channels safely before reducing polyphony.
- Use explicit publisher identity for colliding topics; do not silently merge
  publishers.

## Platform portability

Keep shared protocol, registry, routing, and event logic platform-neutral.
Isolate platform-specific behavior behind narrow adapters for:

- socket handles, errors, close, and nonblocking configuration;
- multicast join/leave and interface selection;
- interface enumeration and local-address discovery;
- monotonic clocks, sleep/wake, and timer behavior;
- worker-thread startup, shutdown, and naming;
- path separators and patch/configuration locations.

Do not introduce POSIX-only APIs into shared code or emulate Windows behavior
with conditional logic spread across the protocol layer. Use the compiler,
SDK, and packaging conventions already required by VCV Rack. A limitation on
one platform must be recorded as an acceptance finding, not silently omitted.

The current validation target is macOS. When Linux or Windows work is
explicitly reopened, use the same source and portable test target rather than
creating separate protocol implementations. If live multicast is unavailable
in the active environment, run all portable tests and document the exact
skipped integration condition.

## Implementation workflow

1. Inspect the repository's existing build, plugin, test, formatting, and
   dependency conventions before adding files or packages.
2. Implement one milestone from `design/poc-design.md` at a time:
   M0 build baseline, M1 protocol, M2 discovery, M3 CV, M4 events/failure
   behavior, M5 UI/persistence/diagnostics, and M6 hardening.
3. Add focused tests with every codec, state-machine, queue, or timing change.
4. Run the current milestone's gate before starting the next milestone.
5. When a milestone's deliverables and gate pass, append `(**COMPLETE**)` to
   its title line in `design/poc-design.md`, separated by one space. For
   example:
   `### M0 — Repository and build baseline (**COMPLETE**)`.
6. Before implementing, ask the user for clarification about any unresolved
   question, requirement, ambiguity, or design decision that could affect
   behavior, scope, compatibility, or acceptance. Do not silently guess.
7. Write clean, maintainable C++ in accordance with established clean-code
   conventions and SOLID principles. Prefer cohesive classes, explicit
   interfaces, dependency inversion at platform boundaries, descriptive names,
   small functions, single-purpose modules, and minimal duplication. Within
   the limits imposed by VCV Rack module requirements, make code only as
   complex as needed; avoid speculative abstractions, unnecessary indirection,
   and premature generalization.
8. Prefer small, reviewable changes grouped by milestone; avoid unrelated
   refactors.
9. Preserve unrelated work in a dirty worktree. Do not reset, revert, or
   overwrite changes not made for the current task.
10. Update directly related documentation when behavior, defaults, packet fields,
   platform requirements, or acceptance criteria change.
11. Report exact validation commands, passed tests, skipped tests and reasons,
   platform/toolchain results, and any measured timing or packet-loss limits.

## Testing requirements

Portable tests must cover:

- topic validation and exact matching;
- packet round trips, endian behavior, bounds, truncation, and malformed data;
- non-finite voltages and channel counts outside 1–16;
- peer lifecycle, TTL clamping, conflict detection, and service revisions;
- node restart/session fencing;
- CV reconstruction and channel-count changes;
- event ordering, duplicate suppression, late-event policy, and timeouts;
- bounded queue overflow and diagnostics.

Use a deterministic impairment transport to inject loss, duplication,
reordering, delay, burst loss, malformed packets, and worker pressure.
Assertions must verify final logical state, including safe gate release, not
only packet counters.

Integration validation must include:

1. Two local nodes discovering one another.
2. One publisher feeding two subscribers.
3. Sixteen-channel CV preservation.
4. A short trigger surviving a CV publication interval.
5. Lost gate-off recovery.
6. Publisher restart rejecting old-session traffic.
7. Presence expiry independent of CV traffic.
8. Clean worker and socket shutdown.

Run sanitizer, thread-safety, and undefined-behavior checks when the platform
toolchain supports them. Treat real-time safety failures as release blockers.

## Change review checklist

Before considering a change complete, verify:

- The change is within POC scope and tied to a use case or acceptance criterion.
- All input is validated before state mutation.
- No new audio-thread blocking, allocation, I/O, or unbounded work exists.
- All asynchronous ownership and shutdown paths are safe.
- Queue overflow and network failure remain visible.
- macOS implications were considered; Linux and Windows remain documented
  future targets without blocking current work.
- Tests cover normal, malformed, lost, duplicated, reordered, delayed, and
  restarted behavior where applicable.
- User-visible configuration is persisted only when stable and safe; discovered
  runtime state is rebuilt rather than persisted as authoritative.
- Documentation and known limitations match the implementation.