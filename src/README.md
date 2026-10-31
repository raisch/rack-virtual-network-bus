# Source layout

This directory is reserved for the VCV Network Bus implementation.

The planned boundaries are:

- `network/` — platform-neutral protocol, discovery, routing, timing, and
  diagnostics components.
- `platform/` — narrow macOS adapters initially, with Linux and Windows
  adapters added when those future deliverables are reopened.
- `modules/` — VCV Rack-facing publisher, subscriber, and browser modules.

M0 intentionally adds no networking behavior. Production source files should
be added as their milestone is implemented, with tests in `tests/`.
