<!--
*******************************************************************************
Copyright (c) 2026 Contributors to the Eclipse Foundation

See the NOTICE file(s) distributed with this work for additional
information regarding copyright ownership.

This program and the accompanying materials are made available under the
terms of the Apache License Version 2.0 which is available at
https://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
*******************************************************************************
-->

# TC8 SOME/IP Conformance Tests

Tests for the S-CORE SOME/IP Gateway based on
[OPEN Alliance TC8](https://www.opensig.org/about/specifications/).

For architecture diagrams and design rationale, see
`docs/architecture/tc8_conformance_testing.rst`.

## Test Scopes

| Scope | Description | DUT | Status |
|---|---|---|---|
| **Protocol Conformance** | Wire-level SOME/IP (SD, messages, events, fields) | `someipd` standalone | Implemented (SD + MSG + EVT + FLD) |
| **Application-Level Tests** | End-to-end via mw::com through the gateway | gatewayd + someipd + C++ apps | [Planned](application/README.md) |

Protocol conformance tests send/receive raw UDP packets using the Python `someip` library.
Application-level tests use C++ `mw::com` applications and work with any SOME/IP binding.

For design rationale, UML dependency diagrams, specification alignment analysis, and
coverage status see `docs/architecture/tc8_conformance_testing.rst`.

## Quick Start

TC8 tests require explicit opt-in via the ``TC8_HOST_IP`` environment
variable. Without it, ``bazel test //...`` gracefully skips all TC8 targets.

```bash
# Run all TC8 tests (loopback — requires multicast route, see below)
bazel test --test_env=TC8_HOST_IP=127.0.0.1 //tests/tc8_conformance/...

# Run a specific target
bazel test --test_env=TC8_HOST_IP=127.0.0.1 //tests/tc8_conformance:tc8_service_discovery

# Run all TC8 tests by tag
bazel test --test_env=TC8_HOST_IP=127.0.0.1 --test_tag_filters=tc8 //tests/...

# Use a real network interface (no multicast route needed)
bazel test --test_env=TC8_HOST_IP=192.168.x.x //tests/tc8_conformance/...
```

> **Note:** ``bazel test //...`` without ``--test_env=TC8_HOST_IP=...`` will
> skip all TC8 tests with a clear message. This is by design — TC8 tests
> need network prerequisites that may not be present in every environment.

## Network Setup

Tests join multicast group `224.244.224.245:30490`.

| Environment | ``TC8_HOST_IP`` | Multicast route? | Tests run? |
|---|---|---|---|
| Real NIC | ``192.168.x.x`` | Not needed | ✅ Yes |
| Loopback | ``127.0.0.1`` | ``sudo ip route add 224.0.0.0/4 dev lo`` | ✅ Yes |
| Loopback, no route | ``127.0.0.1`` | Missing | ⏭️ Skip |
| Not set | — | — | ⏭️ Skip |
| Malformed | e.g. ``abc`` | — | ⏭️ Skip |

```bash
# Required on loopback (run once per boot)
sudo ip route add 224.0.0.0/4 dev lo
```

The ``require_tc8_environment`` fixture in ``conftest.py`` validates all three
prerequisites (env var presence, IP format, multicast route) and skips the
entire module with an actionable message when any check fails.

## Configuration Templates

Each TC8 test area uses a SOME/IP stack config template. The DUT fixture
calls `render_someip_config()` to replace `__TC8_HOST_IP__` and `__TC8_SD_PORT__`
placeholders with the test host IP and a dynamically allocated SD port before
starting `someipd`.

| Template | Used by | Key differences |
|---|---|---|
| `config/tc8_someipd_sd.json` | SD, SD-phases | Event `0x0777` (`is_field: true`, 2 s cycle), eventgroup `0x4455`, `cyclic_offer_delay=2000ms`, initial delay 10–100 ms, repetitions max 3; no TCP reliable port |
| `config/tc8_someipd_service.json` | MSG, EVT, FLD, TCP | Both events (0x0777 field + 0x0778 TCP-reliable), all 3 eventgroups (UDP 0x4455, multicast 0x4465, TCP 0x4475), TCP reliable port 30510, `cyclic_offer_delay=500ms` |

### Common Parameters

| Parameter | Value | Purpose |
|---|---|---|
| Service ID | `0x1234` | DUT test service |
| Instance ID | `0x5678` | DUT test instance |
| SD multicast | `224.244.224.245` | SD capture endpoint |
| SD port | Dynamic (session-scoped) | Allocated at session start; enables parallel execution |
| Service UDP port | `30509` | SOME/IP data endpoint |
| Service TCP port | `30510` | TCP transport endpoint |
| `initial_delay_min/max` | 10–100 ms | SD phase tests |
| `ttl` | 30 s | Prevents expiry mid-test |

### Creating a New Config

Copy `tc8_someipd_sd.json` and change only the parameters that differ.
Keep `__TC8_HOST_IP__` and `__TC8_SD_PORT__` as placeholders so the fixture
can render them at test time.

## Helper Modules

| Module | Purpose |
|---|---|
| `helpers/sd_helpers.py` | SD multicast capture and `SOMEIPSDEntry` parsing |
| `helpers/sd_sender.py` | SD packet building (Find, Subscribe), unicast capture, auto-incrementing session IDs |
| `helpers/someip_assertions.py` | Field-level assertions for SD entries and SOME/IP response headers |
| `helpers/timing.py` | Timestamped offer capture for phase/cyclic analysis |
| `helpers/message_builder.py` | SOME/IP REQUEST/REQUEST_NO_RETURN packet construction and malformed packets |
| `helpers/event_helpers.py` | Event subscription (subscribe + wait Ack) and NOTIFICATION capture |
| `helpers/field_helpers.py` | Field GET/SET request helpers over UDP |

## Adding a New Test

Every new test follows this pattern:

1. **Create a config template** — copy `config/tc8_someipd_sd.json`, keep the
   `__TC8_HOST_IP__` placeholder, and adjust service/event/timing parameters.

2. **Add or extend helpers** — put reusable socket operations, packet builders,
   and assertions in `helpers/`. Reuse existing helpers; do not duplicate.

3. **Write the test module** — name it `test_<tc8_area>.py`. Set `SOMEIP_CONFIG`
   so the `someipd_dut` fixture picks up the right config:

   ```python
   SOMEIP_CONFIG = "tc8_someipd_service.json"
   ```

   Decorate each test with `@add_test_properties` from `score_pytest` for
   ISO 26262 traceability:

   ```python
   from attribute_plugin import add_test_properties

   @add_test_properties(
       fully_verifies=["comp_req__tc8_conformance__<id>"],
       test_type="requirements-based",
       derivation_technique="requirements-analysis",
   )
   def test_tc8_xxx(self, ...) -> None:
       """Docstring is mandatory (enforced by the plugin)."""
   ```

4. **Register a Bazel target** — add a `score_py_pytest` entry in `BUILD.bazel`
   with `env_inherit = ["TC8_HOST_IP"]` and tags `["tc8", "conformance"]`:

   ```python
   score_py_pytest(
       name = "tc8_message_format",
       size = "medium",
       srcs = ["test_someip_message_format.py", "conftest.py"]
            + glob(["helpers/*.py"]),
       data = glob(["config/*.json"]),
       deps = ["//src/someipd", ...],
       env_inherit = ["TC8_HOST_IP"],
       tags = ["tc8", "conformance"],
       target_compatible_with = ["@platforms//os:linux"],
   )
   ```

5. **Add a requirement** — create a `comp_req` in
   `docs/tc8_conformance/requirements.rst` referencing the TC8 clause.

### Helper Conventions

- One concern per module.
- Public functions only — no classes unless a `contextmanager` or `dataclass` is genuinely simpler.
- Callers close returned sockets (or use a `with`/`contextmanager` wrapper).
- Default timeouts: 5 s for single-message capture, 20 s for multi-message timing.
- Use `someip.header` for parsing and building. See `sd_sender.py` for the canonical pattern.

## Directory Structure

```
tests/tc8_conformance/
├── BUILD.bazel                        # Protocol conformance score_py_pytest targets
├── README.md                          # This file
├── conftest.py                        # Fixtures: someipd_dut, host_ip, tester_ip
├── test_service_discovery.py          # TC8-SD-001 … 008, 011, 013, 014
├── test_sd_phases_timing.py           # TC8-SD-009 / 010
├── test_sd_reboot.py                  # TC8-SD-012
├── test_someip_message_format.py      # TC8-MSG-001 … 008
├── test_event_notification.py         # TC8-EVT-001 … 006
├── test_field_conformance.py          # TC8-FLD-001 … 004
├── config/
│   ├── tc8_someipd_sd.json            # SD config template (slow 2 s cycle)
│   └── tc8_someipd_service.json       # Service config: MSG + EVT + FLD + TCP
├── helpers/
│   ├── __init__.py
│   ├── constants.py                   # Shared port/address constants
│   ├── sd_helpers.py                  # SD capture + parsing
│   ├── sd_sender.py                   # SD packet building + unicast capture
│   ├── someip_assertions.py           # Assertion helpers (SD + MSG)
│   ├── timing.py                      # Timestamped capture
│   ├── message_builder.py             # SOME/IP message construction
│   ├── event_helpers.py               # Event subscription + capture
│   └── field_helpers.py               # Field GET/SET helpers
└── application/                       # Enhanced testability (planned)
    ├── README.md
    ├── apps/                          # C++ test apps (planned)
    │   ├── tc8_service/               #   mw::com skeleton
    │   ├── tc8_client/                #   mw::com proxy
    │   └── config/                    #   mw_com + SOME/IP configs
    └── helpers/
        └── process_orchestrator.py    # Multi-process lifecycle (planned)
```
