# Network Interface Selection Design

## Status

Implementation design for the VCV Network Bus proof of concept.

## Scope

This document defines how the VCV Network Bus selects the local network
interface used by discovery and transport. It extends the architecture in
[`poc-design.md`](./poc-design.md).

The design applies to:

- vDNS discovery multicast;
- vCVB CV, gate, and trigger transport;
- explicitly selected host-directed traffic;
- publisher and subscriber modules in one Rack process.

The current implementation target is macOS. Linux and Windows should use the
same logical configuration and runtime contracts when those deliverables are
implemented.

## 1. Design decisions

### 1.1 One setting per Rack process

Network interface selection is process-wide. All publisher and subscriber
modules in one Rack process use the same selected interface.

The existing process-wide `BusRuntime` is the natural owner of this setting.
Independent interface selection per module is explicitly out of scope for the
POC because it would require separate socket runtimes, discovery registries,
worker lifecycles, and diagnostics for every module.

### 1.2 Dropdown selection

The user selects an interface from a dropdown populated from detected local
interfaces. Manual IP address entry is not the primary configuration path.

Each entry should display:

```text
<display name> (<system name>) — <IPv4 address>
```

Examples:

```text
Automatic
Wi-Fi (en0) — 192.168.1.42
Ethernet (en1) — 10.0.0.15
Thunderbolt Bridge (bridge0) — 192.168.10.2
```

The runtime stores the stable system interface name, not the localized display
name or only the IP address.

### 1.3 Automatic is the default

`Automatic` is the default and is persisted as an explicit automatic mode.
The runtime chooses the active interface associated with the system's default
route, subject to the filtering rules in Section 4.

The UI must show the resolved interface when automatic selection is active:

```text
Network: Automatic → Wi-Fi (en0)
```

This prevents the selected path from being ambiguous during troubleshooting.

### 1.4 Discovery and transport use the same interface

The selected interface applies consistently to:

```text
Discovery multicast: 239.192.0.10:47000
Transport multicast: 239.192.0.11:47001
```

The same interface is used for:

- socket binding;
- multicast group membership;
- multicast output selection;
- host-directed UDP traffic;
- local interface diagnostics.

## 2. User interface

### 2.1 Publisher

The publisher should show the shared interface state and provide access to the
shared selector through its context menu or a compact network control:

```text
V-CVB

Host
[ studio-mac                         ]

Network
[ Wi-Fi (en0)                        ]

Publish destinations
[ Topic ▼        ] [ vcvb/cv        ] [-]
[ Host ▼         ] [ mix-room       ] [-]
[ Host + Topic ▼ ] [ drum-machine ] [gate] [-]

[ Add destination ]

                         CV input
```

The local `Host` field identifies the publisher. It is not the selected
interface and must not be used as a substitute for interface configuration.

### 2.2 Subscriber

The subscriber should expose the same shared network state and use independent
logical source and topic filters:

```text
V-CVB

Network
[ Wi-Fi (en0)                        ]

Source host
[ Any host ▼                         ]

Topic
[ vcvb/cv                            ]

                         CV output
```

Changing the interface from either module updates the shared process-wide
setting. Every module should display the new state after the runtime applies
it.

### 2.3 Context-menu behavior

The context menu is the preferred POC configuration surface because it avoids
duplicating a large configuration panel on every module:

```text
Network interface
  ● Automatic → Wi-Fi (en0)
    Wi-Fi (en0) — 192.168.1.42
    Ethernet (en1) — 10.0.0.15
```

The menu should show a disabled status item while the runtime is changing
interfaces:

```text
Network: switching...
```

If the current selection cannot be applied:

```text
Network: unavailable
```

The error should also be available through the normal Rack diagnostic/context
menu path. It must not be silently replaced with automatic selection.

### 2.4 Interface entry state

An interface entry may be:

- **Available** — active, has a usable IPv4 address, and can be selected.
- **Unavailable** — previously selected but no longer present.
- **Unsupported** — present but has no usable address for this POC.

Unavailable and unsupported entries may be shown for diagnostics but must not
be selectable.

## 3. Configuration model

The shared configuration should be represented independently of Rack widgets:

```cpp
enum class InterfaceSelectionMode {
  kAutomatic,
  kExplicit
};

struct NetworkInterfaceSelection {
  InterfaceSelectionMode mode;
  std::string interface_name;
};
```

Runtime status should be separate from requested configuration:

```cpp
struct NetworkInterfaceStatus {
  bool applying;
  bool available;
  std::string requested_interface;
  std::string resolved_interface;
  std::string display_name;
  std::string ipv4_address;
  std::string error;
};
```

The requested interface and the resolved interface must not be conflated.
For example:

```text
Requested: Automatic
Resolved:  en0
```

## 4. Interface enumeration

### 4.1 Common logical contract

The platform adapter should enumerate records with:

```text
system name
display name
IPv4 address
interface index, if available
is up
is loopback
supports multicast, if known
```

The shared runtime should consume this logical representation rather than
calling platform APIs directly.

### 4.2 macOS implementation

The macOS adapter may use POSIX interface APIs such as `getifaddrs()` and
interface flags from `<net/if.h>`. It must:

1. Enumerate IPv4 addresses.
2. Group addresses by system interface name.
3. Read interface-up and multicast-capable flags.
4. Exclude unusable addresses.
5. Preserve the system interface name such as `en0`, `en1`, or `bridge0`.
6. Return deterministic ordering for stable UI and tests.

The display name may be derived from the system interface name for the POC.
Localized hardware names are optional and must not affect persistence.

### 4.3 Filtering rules

The default list should exclude:

- loopback-only interfaces;
- interfaces with no IPv4 address;
- interfaces that are down;
- link-local-only interfaces unless explicitly required for local testing.

The adapter should not exclude a valid active virtual interface solely because
its name is unfamiliar. Users may need bridge, VPN, or Thunderbolt interfaces.

## 5. Socket behavior

### 5.1 Binding

Discovery and transport sockets must bind according to the resolved interface
and configured ports. Binding to `INADDR_ANY` may still be necessary for
receiving multicast, but multicast membership and output selection must use the
selected interface explicitly.

### 5.2 Multicast membership

For each multicast socket:

1. Create the UDP socket.
2. Enable the required reuse options.
3. Bind to the configured port.
4. Join the multicast group on the selected interface.
5. Set the outgoing multicast interface.
6. Apply the configured TTL and loopback behavior.

The discovery and transport groups must not accidentally join on different
interfaces.

### 5.3 Host-directed traffic

Host-directed traffic uses the same selected local interface. The destination
host's address may be discovered through the peer registry, but local egress
selection remains controlled by the process-wide interface setting.

## 6. Applying a change

Interface changes are asynchronous and must not run on Rack's audio thread.

The requested change should follow this sequence:

```text
UI requests interface change
        |
        v
Configuration service validates the request
        |
        v
Network worker stops discovery and transport
        |
        v
Old multicast memberships and sockets are closed
        |
        v
The new interface is resolved and validated
        |
        v
New sockets are opened and multicast groups joined
        |
        v
Discovery advertisements and transport resume
        |
        v
Updated status is published to the UI
```

If any step fails:

- retain the requested selection;
- report the failure explicitly;
- mark the runtime unavailable;
- do not silently select a different interface;
- continue Rack module processing without blocking the audio thread.

Automatic mode may re-resolve after an interface change notification or a
bounded periodic refresh. It must not recreate sockets continuously while the
network is stable.

## 7. Threading and real-time constraints

The following operations are prohibited on Rack's audio thread:

- interface enumeration;
- DNS or peer lookup;
- socket creation or closure;
- multicast join/leave;
- configuration locks that may block;
- string allocation;
- UI updates.

The audio thread may only interact with bounded, pre-existing transport state.
The network worker owns socket lifecycle and interface transitions. The UI
reads a snapshot of `NetworkInterfaceStatus` and submits configuration
requests through a bounded or otherwise non-blocking control path.

## 8. Persistence

Because selection is process-wide, it must not be independently persisted by
every publisher and subscriber module.

The preferred POC storage is plugin-level settings:

```json
{
  "networkInterface": {
    "mode": "automatic",
    "name": ""
  }
}
```

An explicit selection is stored as:

```json
{
  "networkInterface": {
    "mode": "explicit",
    "name": "en0"
  }
}
```

The display name and IP address must not be persisted as authoritative
identity because either can change between launches.

If plugin-level Rack settings are not available in the initial implementation,
the POC may use a shared configuration file. A module-owned patch field is not
recommended because multiple modules can produce conflicting values.

## 9. Diagnostics

Diagnostics should expose at least:

```text
Requested interface: Automatic
Resolved interface: en0
IPv4 address: 192.168.1.42
Discovery socket: ready
Transport socket: ready
Last transition: successful
```

On failure:

```text
Requested interface: en1
Resolved interface: unavailable
Discovery socket: stopped
Transport socket: stopped
Error: selected interface has no usable IPv4 address
```

Diagnostics should include the underlying system error where available, but
must avoid exposing unnecessary sensitive network information in general logs.

## 10. Failure and recovery behavior

### Selected interface disappears

The runtime should:

1. Detect socket errors or interface refresh changes.
2. Mark the interface unavailable.
3. Stop advertising the node until discovery is restored.
4. Keep subscriber outputs in their existing timeout/fail-safe behavior.
5. Retry only through the network worker.

### Automatic interface disappears

Automatic mode should attempt to resolve another eligible interface after a
bounded delay. The UI should show the transition and final resolved interface.

### Explicit interface returns

An explicitly selected interface should be retried after a bounded delay. It
must not be replaced by another interface without user selection.

### No usable interface exists

The runtime should remain initialized but network-unavailable. Rack should not
crash, hang, or fail to load the plugin.

## 11. Testing requirements

### Unit tests

Test the platform-neutral logic for:

- deterministic interface sorting;
- automatic-interface selection;
- filtering of loopback and down interfaces;
- explicit interface lookup;
- unavailable-interface errors;
- requested-versus-resolved status;
- configuration serialization and deserialization.

### Adapter tests

Use injected interface records so tests do not depend on the host machine:

- one active Ethernet interface;
- Wi-Fi plus Ethernet;
- only loopback;
- multiple IPv4 addresses on one interface;
- an explicit interface that disappears;
- an active virtual interface.

### Socket tests

Verify:

- discovery joins the selected interface;
- transport joins the selected interface;
- both sockets use the same resolved interface;
- changing the interface closes old memberships;
- changing the interface recreates sockets;
- failed transitions report an error;
- no socket work runs on the audio thread.

### Manual macOS validation

Validate with:

1. Automatic mode on Wi-Fi.
2. Explicit Ethernet selection.
3. Two Rack processes using the same interface.
4. Switching from Wi-Fi to Ethernet while modules are loaded.
5. Disabling the selected interface.
6. Re-enabling the selected interface.
7. A machine with a VPN or bridge interface present.

The test should record the selected interface, resolved address, discovery
status, transport status, and whether peer recovery succeeds.

## 12. Implementation milestones

### NI-1 — Interface model and enumeration (**COMPLETE**)

Deliver:

- platform-neutral interface records;
- macOS `getifaddrs()` adapter;
- deterministic filtering and sorting;
- automatic-interface resolution;
- unit tests using injected records.

Gate:

- all interface-selection unit tests pass;
- no Rack or socket code depends directly on enumeration details.

### NI-2 — Shared network configuration (**COMPLETE**)

Deliver:

- process-wide configuration service;
- requested/resolved status snapshots;
- plugin-level persistence;
- non-blocking configuration request path.

Gate:

- multiple modules observe one shared selection;
- conflicting per-module settings cannot be created.

### NI-3 — Socket integration (**COMPLETE**)

Deliver:

- selected-interface multicast membership;
- selected-interface multicast output;
- discovery and transport using the same resolved interface;
- asynchronous socket restart on change;
- explicit transition errors.

Gate:

- live multicast tests pass on the selected interface;
- switching interfaces does not crash or block Rack audio processing.

### NI-4 — Rack UI (**COMPLETE**)

Deliver:

- interface dropdown/context menu;
- automatic resolved-interface display;
- switching and unavailable status;
- diagnostics for requested and resolved values.

Gate:

- selection from either module updates both modules;
- the setting survives Rack restart;
- unavailable interfaces produce visible errors.

### NI-5 — Packaging and documentation (**COMPLETE**)

Deliver:

- macOS ARM64 validation;
- macOS x86_64 validation;
- installation and troubleshooting documentation;
- Linux and Windows adapter notes without claiming unsupported builds.

Gate:

- both macOS packages load;
- interface selection behavior is documented;
- the POC acceptance checklist is complete.

## 13. Acceptance checklist

- [ ] Automatic mode is the default.
- [ ] The UI lists usable local interfaces.
- [ ] Entries show display name, system name, and IPv4 address.
- [ ] The stable system interface name is persisted.
- [ ] Discovery and transport use the same interface.
- [ ] Interface selection is process-wide.
- [ ] Changing the selection is asynchronous.
- [ ] No socket or enumeration work occurs on the audio thread.
- [ ] Interface failures are visible and do not silently fall back.
- [ ] Automatic mode can recover when the active interface changes.
- [ ] Explicit mode does not silently switch interfaces.
- [ ] Multiple modules display consistent network status.
- [ ] The selected interface survives restart.
- [ ] ARM64 and x86_64 macOS packages are validated.

## 14. Future extensions

The following are intentionally deferred:

- independent interface selection per module;
- IPv6 interface and multicast support;
- cross-subnet interface routing;
- manual IP address entry as a first-class selector;
- interface-specific transport policy;
- bandwidth and packet counters per interface;
- Linux and Windows native interface display-name integration.
