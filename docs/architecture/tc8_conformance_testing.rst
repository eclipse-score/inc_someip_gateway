..
   # *******************************************************************************
   # Copyright (c) 2026 Contributors to the Eclipse Foundation
   #
   # See the NOTICE file(s) distributed with this work for additional
   # information regarding copyright ownership.
   #
   # This program and the accompanying materials are made available under the
   # terms of the Apache License Version 2.0 which is available at
   # https://www.apache.org/licenses/LICENSE-2.0
   #
   # SPDX-License-Identifier: Apache-2.0
   # *******************************************************************************

TC8 SOME/IP Conformance Testing
================================

Overview
--------

`OPEN Alliance TC8 <https://opensig.org/tech-committee/tc8-automotive-ethernet-ecu-test-specification/>`_
defines conformance tests for automotive SOME/IP implementations.
The TC8 test suite has two scopes:

- **Protocol Conformance** — tests ``someipd`` at the wire level using the
  ``someip`` Python package. No application processes are needed.

- **Application-Level Tests** — tests the full gateway path
  (mw::com client → ``gatewayd`` → ``someipd`` → network) using C++ apps
  built on ``score::mw::com``. These tests are stack-agnostic.

Both scopes live under ``tests/tc8_conformance/`` and share the ``tc8`` /
``conformance`` Bazel tags. For setup instructions and test details, see
``tests/tc8_conformance/README.md``.

Test Scope Overview
-------------------

.. uml::

   @startuml
   !theme plain
   skinparam packageStyle rectangle

   package "Protocol Conformance" {
     [pytest] as L1Test
     [someipd (standalone)] as L1DUT
     L1Test -down-> L1DUT : raw SOME/IP\nUDP / TCP
   }

   package "Application-Level Tests" {
     [pytest] as L2Orch
     [TC8 Client\n(mw::com Proxy)] as L2Client
     [gatewayd] as L2GW
     [someipd] as L2SOMEIP
     [TC8 Service\n(mw::com Skeleton)] as L2Service

     L2Orch .down.> L2Client
     L2Orch .down.> L2Service
     L2Client -down-> L2GW : LoLa IPC
     L2GW -down-> L2SOMEIP : LoLa IPC
     L2SOMEIP -down-> L2Service : SOME/IP\nUDP / TCP
   }
   @enduml

Protocol Conformance
--------------------

Protocol conformance tests exercise the SOME/IP stack at the wire protocol
level. The DUT is ``someipd`` in standalone mode. Tests send raw SOME/IP
messages and verify responses against the TC8 specification.

Standalone Mode
^^^^^^^^^^^^^^^

``someipd`` normally waits for ``gatewayd`` to connect via LoLa IPC before
offering services. The ``--tc8-standalone`` flag removes this dependency:
``someipd`` skips the IPC proxy setup and calls ``offer_service()`` directly.

This keeps protocol conformance tests simple — no process ordering, no
FlatBuffers config, and fewer failure modes. See ``src/someipd/main.cpp``.

Port Isolation and Parallel Execution
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each Bazel TC8 target runs in its own OS process and receives unique SOME/IP
port values via the Bazel ``env`` attribute.  Three environment variables
control port assignment:

``TC8_SD_PORT``
    SOME/IP-SD port.  Set in both the vsomeip config template (replacing the
    ``__TC8_SD_PORT__`` placeholder) and read by the Python SD sender socket
    at module import time via ``helpers/constants.py``.  The SOME/IP-SD
    protocol requires SD messages to originate from the configured SD port;
    satisfying this does not require a fixed port — it requires only that
    both sides use the *same* port, which is guaranteed because both the
    vsomeip config and the Python constants read the same env var.

``TC8_SVC_PORT``
    DUT UDP (unreliable) service port.  Replaces the ``__TC8_SVC_PORT__``
    placeholder in config templates.

``TC8_SVC_TCP_PORT``
    DUT TCP (reliable) service port.  Replaces the ``__TC8_SVC_TCP_PORT__``
    placeholder in config templates.  Only set for targets that use reliable
    transport (``tc8_message_format``, ``tc8_event_notification``,
    ``tc8_field_conformance``).

All three constants default to the historical static values (30490 / 30509 /
30510) when the environment variables are not set, preserving backward
compatibility for local development runs without Bazel.

.. rubric:: Port Assignment per Target

.. list-table::
   :header-rows: 1
   :widths: 30 15 15 20 20

   * - Target
     - TC8_SD_PORT
     - TC8_SVC_PORT
     - TC8_SVC_TCP_PORT
     - exclusive
   * - ``tc8_service_discovery``
     - 30490
     - 30500
     - —
     - no
   * - ``tc8_sd_phases_timing``
     - 30491
     - 30501
     - —
     - yes (timing)
   * - ``tc8_message_format``
     - 30492
     - 30502
     - 30503
     - no
   * - ``tc8_event_notification``
     - 30493
     - 30504
     - 30505
     - no
   * - ``tc8_sd_reboot``
     - 30494
     - 30506
     - —
     - yes (lifecycle)
   * - ``tc8_field_conformance``
     - 30495
     - 30507
     - 30508
     - no
   * - ``tc8_sd_format``
     - 30496
     - 30509
     - —
     - no
   * - ``tc8_sd_robustness``
     - 30497
     - 30510
     - —
     - no
   * - ``tc8_sd_client``
     - 30498
     - 30511
     - —
     - yes (lifecycle)
   * - ``tc8_multi_service``
     - 30499
     - 30512
     - 30513
     - no

The four medium targets (``tc8_service_discovery``, ``tc8_message_format``,
``tc8_event_notification``, ``tc8_field_conformance``) run in parallel.  The
three large/exclusive targets (``tc8_sd_phases_timing``, ``tc8_sd_reboot``,
``tc8_sd_client``) retain the ``exclusive`` tag for timing accuracy or
lifecycle correctness.  The remaining medium targets (``tc8_sd_format``,
``tc8_sd_robustness``, ``tc8_multi_service``) also run in parallel.

Test Module Structure
^^^^^^^^^^^^^^^^^^^^^

Each TC8 area has a test module (pytest) and one or more helper modules.
The diagrams below show the dependencies grouped by TC8 domain.
In both diagrams, blue boxes represent test modules and green boxes
represent shared helper modules. Dashed arrows indicate internal
helper-to-helper dependencies.

Service Discovery (SD)
~~~~~~~~~~~~~~~~~~~~~~~~

The Service Discovery tests (TC8-SD) verify SOME/IP-SD multicast offer
announcements, unicast find/subscribe responses, SD phase timing, byte-level
SD field values, malformed packet robustness, and SD client lifecycle.
Six test modules cover the SD test suite, sharing five helpers for socket
management, SD packet construction, malformed packet injection, assertion,
and timestamped capture.

.. uml::

   @startuml
   !theme plain
   scale max 800 width
   skinparam classAttributeIconSize 0
   skinparam class {
     BackgroundColor<<test>> #E3F2FD
     BorderColor<<test>> #1565C0
     BackgroundColor<<helper>> #E8F5E9
     BorderColor<<helper>> #2E7D32
   }

   title Service Discovery — Test Module Dependencies

   class test_service_discovery <<test>> {
     TC8-SD-001..008, 011, 013, 014
     SOMEIPSRV_SD_MESSAGE_01–06/14–19
     SD_BEHAVIOR_03/04
     ETS_088/091/092/098/099/100/101
     ETS_107/120/122/128/130/155
   }
   class test_sd_phases_timing <<test>> {
     TC8-SD-009 / 010
   }
   class test_sd_reboot <<test>> {
     TC8-SD-012
   }
   class test_sd_format_compliance <<test>> {
     TC8-SDF (SD Format)
     FORMAT_01/02/04–06/09–13/15/16/18–28
     OPTIONS_01/02/03/05/06/08–14
   }
   class test_sd_robustness <<test>> {
     ETS SD Robustness
     Malformed entries, options,
     framing errors, subscribe edges
   }
   class test_sd_client <<test>> {
     ETS SD Client Lifecycle
     ETS_081/082/084
   }

   class sd_helpers <<helper>> {
     +open_multicast_socket()
     +parse_sd_offers()
     +capture_sd_offers()
   }
   class sd_sender <<helper>> {
     +open_sender_socket()
     +send_find_service()
     +send_subscribe_eventgroup()
     +capture_unicast_sd_entries()
     +capture_some_ip_messages()
   }
   class sd_malformed <<helper>> {
     +build_malformed_entry()
     +build_malformed_option()
     +build_truncated_sd()
     +send_malformed_sd()
   }
   class someip_assertions <<helper>> {
     +assert_sd_offer_entry()
     +assert_offer_has_ipv4_endpoint_option()
     +assert_offer_has_tcp_endpoint_option()
   }
   class timing <<helper>> {
     +collect_sd_offers_from_socket()
     +capture_sd_offers_with_timestamps()
   }

   ' layout: test modules in two rows
   test_service_discovery -right[hidden]- test_sd_phases_timing
   test_sd_phases_timing -right[hidden]- test_sd_reboot
   test_sd_format_compliance -right[hidden]- test_sd_robustness
   test_sd_robustness -right[hidden]- test_sd_client
   test_service_discovery -down[hidden]- test_sd_format_compliance

   ' layout: helpers in a row below tests
   sd_helpers -right[hidden]- sd_sender
   sd_sender -right[hidden]- sd_malformed
   someip_assertions -right[hidden]- timing
   sd_helpers -down[hidden]- someip_assertions

   ' test → helper dependencies
   test_service_discovery -down-> sd_helpers
   test_service_discovery -down-> sd_sender
   test_service_discovery -down-> someip_assertions
   test_service_discovery -down-> timing
   test_sd_phases_timing -down-> timing
   test_sd_phases_timing -down-> sd_helpers
   test_sd_reboot -down-> sd_helpers
   test_sd_format_compliance -down-> sd_helpers
   test_sd_robustness -down-> sd_malformed
   test_sd_robustness -down-> sd_helpers
   test_sd_client -down-> sd_helpers
   test_sd_client -down-> sd_sender

   ' internal helper dependencies
   timing ..> sd_helpers : <<uses>>
   @enduml

Message Format, Events, Fields, and TCP Transport (MSG / EVT / FLD / TCP)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The remaining protocol tests are grouped into message format (TC8-MSG),
event notification (TC8-EVT), field access (TC8-FLD), and TCP transport
binding (SOMEIPSRV_RPC / OPTIONS). Each domain has a dedicated test module.
``test_someip_message_format`` has been extended with three additional
classes covering basic service identifiers (``SOMEIPSRV_BASIC_01–03``),
response field assertions (``SOMEIPSRV_ONWIRE_01/02/04/06/11``,
``SOMEIPSRV_RPC_18/20``), and fire-and-forget / error handling
(``SOMEIPSRV_RPC_05–10``, ``ETS_004/054/059/061/075``).
Domain-specific helpers handle packet construction, subscription workflows,
field get/set operations, and TCP stream framing, while ``sd_helpers``
provides shared SD primitives used across all three test modules.

.. uml::

   @startuml
   !theme plain
   scale max 800 width
   skinparam classAttributeIconSize 0
   skinparam class {
     BackgroundColor<<test>> #E3F2FD
     BorderColor<<test>> #1565C0
     BackgroundColor<<helper>> #E8F5E9
     BorderColor<<helper>> #2E7D32
   }

   title Message / Event / Field / TCP — Test Module Dependencies

   class test_someip_message_format <<test>> {
     TC8-MSG-001..008
     SOMEIPSRV_RPC_01/02
     SOMEIPSRV_OPTIONS_15
     SOMEIPSRV_BASIC_01–03
     SOMEIPSRV_ONWIRE_01/02/04/06/11
     SOMEIPSRV_RPC_05–10/18/20
     ETS_004/054/059/061/075
   }
   class test_event_notification <<test>> {
     TC8-EVT-001..006
     SOMEIPSRV_RPC_17 (TCP)
   }
   class test_field_conformance <<test>> {
     TC8-FLD-001..004
     SOMEIPSRV_RPC_17 (TCP)
   }

   class message_builder <<helper>> {
     +build_request()
     +build_request_no_return()
     +build_truncated_message()
     +build_wrong_protocol_version_request()
     +build_oversized_message()
   }
   class someip_assertions <<helper>> {
     +assert_valid_response()
     +assert_return_code()
     +assert_session_echo()
     +assert_client_echo()
     +assert_offer_has_tcp_endpoint_option()
   }
   class sd_helpers <<helper>> {
     +open_multicast_socket()
     +capture_sd_offers()
   }
   class sd_sender <<helper>> {
     +open_sender_socket()
     +send_subscribe_eventgroup()
     +capture_unicast_sd_entries()
   }
   class event_helpers <<helper>> {
     +subscribe_and_wait_ack()
     +subscribe_and_wait_ack_tcp()
     +capture_notifications()
     +capture_any_notifications()
     +assert_notification_header()
   }
   class field_helpers <<helper>> {
     +send_get_field()
     +send_set_field()
     +send_get_field_tcp()
     +send_set_field_tcp()
   }
   class tcp_helpers <<helper>> {
     +tcp_connect()
     +tcp_send_request()
     +tcp_receive_response()
     +tcp_listen()
     +tcp_accept_and_receive_notification()
   }
   class udp_helpers <<helper>> {
     +udp_send_concatenated()
   }

   ' layout: test modules in a row
   test_someip_message_format -right[hidden]- test_event_notification
   test_event_notification -right[hidden]- test_field_conformance

   ' layout: helpers in grid
   message_builder -right[hidden]- someip_assertions
   sd_helpers -right[hidden]- sd_sender
   event_helpers -right[hidden]- field_helpers
   tcp_helpers -right[hidden]- event_helpers
   udp_helpers -right[hidden]- tcp_helpers
   message_builder -down[hidden]- sd_helpers
   sd_helpers -down[hidden]- tcp_helpers
   tcp_helpers -down[hidden]- event_helpers

   ' test → helper dependencies
   test_someip_message_format -down-> message_builder
   test_someip_message_format -down-> someip_assertions
   test_someip_message_format -down-> sd_helpers
   test_someip_message_format -down-> tcp_helpers
   test_someip_message_format -down-> udp_helpers
   test_event_notification -down-> event_helpers
   test_event_notification -down-> sd_helpers
   test_event_notification -down-> sd_sender
   test_event_notification -down-> tcp_helpers
   test_field_conformance -down-> field_helpers
   test_field_conformance -down-> event_helpers
   test_field_conformance -down-> sd_helpers

   ' internal helper dependencies
   event_helpers ..> sd_sender : <<uses>>
   field_helpers ..> message_builder : <<uses>>
   field_helpers ..> tcp_helpers : <<uses>>
   @enduml

Multi-service and Multi-instance
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``test_multi_service.py`` verifies that ``someipd`` correctly handles
vsomeip configurations that declare multiple service entries, and that
each service instance advertises its own distinct UDP port in the SD
endpoint option.

- ``SOMEIPSRV_RPC_13`` — confirms that the vsomeip config successfully
  loads multiple service entries and that the DUT offers its primary
  service (0x1234/0x5678) with a well-formed SD stream.
- ``SOMEIPSRV_RPC_14`` — confirms that each service instance in the
  config is assigned a distinct UDP port and that the offered service's
  SD IPv4 endpoint option reflects the configured port.

The DUT uses ``config/tc8_someipd_multi.json`` — a vsomeip configuration
that declares two services (0x1234/instance 0x5678 and 0x5678/instance
0x0001) on separate ports, ensuring port routing correctness at the SD layer.

.. uml::

   @startuml
   !theme plain
   skinparam classAttributeIconSize 0
   skinparam class {
     BackgroundColor<<test>> #E3F2FD
     BorderColor<<test>> #1565C0
     BackgroundColor<<helper>> #E8F5E9
     BorderColor<<helper>> #2E7D32
   }

   title Multi-service / Multi-instance — Test Module Dependencies

   class test_multi_service <<test>> {
     SOMEIPSRV_RPC_13
     SOMEIPSRV_RPC_14
   }

   class sd_helpers <<helper>> {
     +open_multicast_socket()
     +capture_sd_offers()
   }
   class sd_sender <<helper>> {
     +open_sender_socket()
     +send_find_service()
   }

   ' layout
   sd_helpers -right[hidden]- sd_sender

   ' dependencies
   test_multi_service -down-> sd_helpers
   test_multi_service -down-> sd_sender
   @enduml

All requirement IDs use the ``comp_req__tc8_conformance__`` prefix.

Application-Level Tests
-----------------------

Application-level tests verify the full gateway pipeline end-to-end.
A **service** (mw::com Skeleton) and **client** (mw::com Proxy) communicate
through ``gatewayd`` + ``someipd``. Because both apps use the mw::com API
only, the same test code works with any SOME/IP binding.

.. note::

   Application-level tests are planned. See
   ``tests/tc8_conformance/application/README.md`` for the intended scope.

Planned Topology
^^^^^^^^^^^^^^^^

.. uml::

   @startuml
   !theme plain

   node "Host" {
     [TC8 Service\n(mw::com Skeleton)] as Svc
     [gatewayd] as GW
     [someipd] as SD
     [TC8 Client\n(mw::com Proxy)] as Cli

     Svc -right-> GW : LoLa IPC
     GW -right-> SD : LoLa IPC
     SD -right-> Cli : SOME/IP\nUDP / TCP
   }

   [pytest\norchestrator] as Orch
   Orch .down.> Svc
   Orch .down.> GW
   Orch .down.> SD
   Orch .down.> Cli
   @enduml

Stack-Agnostic Design
^^^^^^^^^^^^^^^^^^^^^

The test apps depend only on ``score::mw::com``. Switching the SOME/IP stack
requires changing the deployment config, not test code.

.. uml::

   @startuml
   !theme plain

   package "Test Code (stack-agnostic)" {
     class "TC8 Service" {
       mw::com Skeleton
       events, fields
     }
     class "TC8 Client" {
       mw::com Proxy
       subscribe, read
     }
   }

   package "Deployment Config (stack-specific)" {
     class "mw_com_config.json" {
       binding: <stack A> | <stack B>
     }
     class "someip_stack.json" {
       service routing
     }
   }

   package "Runtime (swappable)" {
     class "someipd\n(Stack A)" as vS
     class "someipd\n(Stack B)" as eS
   }

   "TC8 Service" ..> "mw_com_config.json" : reads at startup
   "TC8 Client" ..> "mw_com_config.json" : reads at startup
   "mw_com_config.json" ..> vS : binds to
   "mw_com_config.json" ..> eS : or binds to

   note bottom of "TC8 Service"
     Swapping stacks = change
     config only.
     Zero code changes.
   end note
   @enduml

Planned Components
^^^^^^^^^^^^^^^^^^

.. uml::

   @startuml
   !theme plain
   skinparam classAttributeIconSize 0

   class "Enhanced Testability Service" as ETS {
     mw::com Skeleton
     --
     +offer_tc8_events()
     +offer_tc8_fields()
   }

   class "Enhanced Testability Client" as ETC {
     mw::com Proxy
     --
     +subscribe_events()
     +read_fields()
     +validate_tc8_values()
   }

   class "Test Orchestrator" as TO {
     pytest
     --
     +run_end_to_end_tests()
   }

   class "Process Orchestrator" as PO {
     helper
     --
     +start_stack(config_dir)
     +stop_stack(handle)
     +wait_service_available(name, timeout)
   }

   TO -down-> ETS : starts / stops
   TO -down-> ETC : starts / stops
   TO -down-> PO : uses
   @enduml

The orchestrator starts the ETS application, ``gatewayd``, and ``someipd``
via ``conftest.py`` subprocess fixtures — the same ``subprocess.Popen``
pattern used for the standalone ``someipd`` fixture. The S-CORE ITF
framework is the preferred long-term orchestrator for multi-node or
structured CI reporting scenarios.

CI/CD Integration
-----------------

Protocol conformance tests run on ``ubuntu-24.04`` GitHub Actions runners
under ``build_and_test_host.yml``.

Bazel Configuration
^^^^^^^^^^^^^^^^^^^^

TC8 tests are opt-in via ``.bazelrc`` configs::

    # Default: bazel test //... excludes TC8 (no prerequisites needed)
    test --test_tag_filters=-tc8

    # Opt-in: bazel test --config=tc8 //tests/tc8_conformance/...
    test:tc8 --test_tag_filters=tc8
    test:tc8 --test_env=TC8_HOST_IP=127.0.0.1
    test:tc8 --run_under=//tests/tc8_conformance:tc8_net_wrapper

The ``--config=tc8`` flag does three things:

1. Overrides the default tag filter to select TC8 targets.
2. Sets ``TC8_HOST_IP=127.0.0.1`` via ``--test_env``.
3. Wraps each test in ``tc8_net_wrapper.sh`` via ``--run_under``.

Network Namespace Wrapper
^^^^^^^^^^^^^^^^^^^^^^^^^^

The wrapper script (``tests/tc8_conformance/tc8_net_wrapper.sh``) uses
``unshare`` to create a **private network namespace** per test process
with loopback and multicast routing — no ``sudo`` required.

All child processes (including ``someipd`` spawned by conftest.py and, in
future, ``gatewayd`` and the ETS application) **inherit the namespace**
because they are started via ``subprocess.Popen`` within the wrapped
process.  Each test target runs in its own isolated namespace — no port
conflicts between concurrent targets.

If namespace creation fails, the wrapper falls back to direct execution
with a warning.  The ``require_tc8_environment`` fixture detects the
missing multicast route and skips gracefully.

CI Workflow
^^^^^^^^^^^^

The CI workflow (``build_and_test_host.yml``) uses two test steps::

    # Step 1: all tests except TC8 (tag filter is in .bazelrc default)
    bazel test //... --build_tests_only

    # Step 2: TC8 conformance tests (self-configuring network namespace)
    bazel test --config=tc8 --test_output=all //tests/tc8_conformance/...

No ``sudo ip route add`` is needed — the wrapper handles it.

Environment-Aware Skip Logic
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

TC8 tests are designed to **skip gracefully** when the environment is not
ready, so that ``bazel test //...`` never fails due to TC8 prerequisites.
Two layers of protection exist:

1. **Tag filter exclusion** — ``.bazelrc`` sets
   ``test --test_tag_filters=-tc8`` so ``bazel test //...`` does not even
   attempt to run TC8 targets.

2. **Fixture guard** — if TC8 targets are run without the namespace (e.g.,
   via ``--test_env=TC8_HOST_IP=...`` without ``--config=tc8``), the
   ``require_tc8_environment`` autouse fixture in ``conftest.py`` checks
   three conditions:

   a. **Opt-in gate** — ``TC8_HOST_IP`` must be present in the environment.
   b. **IP validation** — ``TC8_HOST_IP`` must be a valid IPv4 address.
   c. **Multicast route** — when using a loopback address, the fixture
      verifies that ``ip route get 224.244.224.245`` resolves to ``dev lo``.

   If any check fails, the module skips with an actionable message.

Port Isolation and Parallelism
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each TC8 target receives unique ``TC8_SD_PORT``, ``TC8_SVC_PORT``, and
(where applicable) ``TC8_SVC_TCP_PORT`` values via the Bazel ``env``
attribute, as described in the Port Isolation and Parallel Execution section
above.  The medium targets (``tc8_service_discovery``, ``tc8_message_format``,
``tc8_event_notification``, ``tc8_field_conformance``, ``tc8_sd_format``,
``tc8_sd_robustness``, ``tc8_multi_service``) run concurrently.  The three
exclusive targets (``tc8_sd_phases_timing``, ``tc8_sd_reboot``,
``tc8_sd_client``) carry the ``exclusive`` tag and run serially for timing
accuracy or lifecycle correctness.

Application-level tests (when implemented) will follow the same pattern.
If multi-node isolation is needed, the Docker Compose setup at
``tests/integration/docker_setup/`` can be extended.

.. _network_configurations:

Network Configurations
^^^^^^^^^^^^^^^^^^^^^^^

Two network configurations are supported.  The choice depends on what test
categories need to run.

.. list-table::
   :header-rows: 1
   :widths: 20 30 25 25

   * - Configuration
     - Command
     - Network
     - Multicast
   * - **Loopback** (default, CI)
     - ``bazel test --config=tc8 //tests/tc8_conformance/...``
     - Private namespace, ``lo`` only
     - Automatic (wrapper)
   * - **Non-loopback interface**
     - ``bazel test --test_env=TC8_HOST_IP=<ip> //tests/tc8_conformance/...``
     - Host network, named interface (e.g. ``eth0``)
     - Native (kernel routes multicast via the interface)

**Loopback** is the default for CI and local development.  All processes
(pytest, ``someipd``, and future ``gatewayd`` / ETS application) run inside
an isolated network namespace with loopback multicast.

**Non-loopback interface** means a named interface (``eth0``, ``ens0``,
``genet0``, etc.) with a routable IP address — as opposed to ``lo`` /
``127.0.0.1``.  This is required for tests that exercise vsomeip behaviour
that differs between loopback and a real interface:

- **OPTIONS_08–14** (IPv4 Multicast Option sub-fields): vsomeip 3.6.1 does
  not include ``IPv4MulticastOption`` in SubscribeEventgroupAck when bound
  to a loopback address.  These 7 tests skip automatically on loopback and
  require ``TC8_HOST_IP`` set to a non-loopback address.
- **ETS_150** (``triggerEventUINT8Multicast``): multicast event delivery may
  behave differently on loopback vs. a named interface depending on the
  SOME/IP stack's multicast group join implementation.

The non-loopback configuration does **not** use the ``--run_under`` wrapper
(no namespace needed — the host kernel handles multicast routing natively).
It also does not require ``sudo`` — multicast is routed by default on
non-loopback interfaces.

Impact on Future ETS Application-Level Tests
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The 49 blocked ETS tests (see `ETS Application Gap`_) require a 4-process
topology: pytest → TC8 Service + ``gatewayd`` + ``someipd`` + TC8 Client.
The network namespace wrapper is **compatible** with this topology because
all four processes are spawned as subprocesses and inherit the namespace:

.. list-table::
   :header-rows: 1
   :widths: 30 10 25 35

   * - ETS Category
     - Count
     - Network Need
     - Loopback Compatible?
   * - Serialization / Echo (ETS_001–053, 063–073)
     - 44
     - LoLa IPC + loopback SOME/IP
     - ✅ Yes — all processes in same namespace
   * - Control methods (ETS_089, 164)
     - 2
     - Same as above
     - ✅ Yes
   * - Event triggers (ETS_146–151)
     - 6
     - Loopback UDP/TCP events
     - ✅ Yes — multicast via ``lo``
   * - Field accessors (ETS_166–168)
     - 3
     - Loopback field access
     - ✅ Yes

The ``conftest.py`` subprocess fixture pattern (``launch_someipd`` /
``terminate_someipd``) will be extended for the ETS application and
``gatewayd``.  No wrapper changes are needed — new child processes
automatically inherit the calling process's network namespace.

**Multicast event tests** (``ETS_150 triggerEventUINT8Multicast``,
``ETS_104 SD_ClientServiceGetLastValueOfEventUDPMulticast``): these tests
exercise multicast event delivery to group ``239.0.0.1:40490`` (configured
in eventgroup ``0x4465``).  On loopback, multicast group join
(``IP_ADD_MEMBERSHIP``) and multicast send (``IP_MULTICAST_IF``) both work
within the private namespace.  The wrapper's ``ip route add 224.0.0.0/4 dev
lo`` covers the entire Class D range (``224.0.0.0`` through
``239.255.255.255``), including ``239.0.0.1``.

**Tests that will continue to skip on loopback**: OPTIONS_08–14 (7 tests)
skip because vsomeip 3.6.1 omits ``IPv4MulticastOption`` from
SubscribeEventgroupAck when bound to loopback.  This is a vsomeip stack
behaviour, not a namespace or routing limitation.  These tests pass on a
non-loopback interface.

TC8 Specification Alignment Analysis
-------------------------------------

This section maps the 230 test cases in Chapter 5 of the
`OPEN Alliance TC8 ECU Test Specification Layer 3-7 v3.0 (October 2019)
<https://opensig.org/tech-committee/tc8-automotive-ethernet-ecu-test-specification/>`_
to the current implementation status. It answers three questions for every
TC8 group:

1. **What is already tested and passing?**
2. **What can be tested today without any new software?**
3. **What requires new software before the tests can run?**

For the full test case catalog see
``tests/tc8_conformance/tc8_ecu_test_chapter5_someip_v3.0_oct2019.md``.

The specification organizes Chapter 5 into two top-level groups:

- **SOME/IP Server Tests** (``SOMEIPSRV_*``, 93 items, Section 5.1.5) —
  wire-level protocol checks. Only ``someipd`` and a raw socket are needed.
  No application code is required.
- **Enhanced Testability Service Tests** (``SOMEIP_ETS_*``, 137 items,
  Section 5.1.6) — behavior tests that range from wire-level SD tests
  (needing only ``someipd``) to full-pipeline serialization tests that
  require a C++ test application.

Coverage at a Glance
^^^^^^^^^^^^^^^^^^^^^

The table below shows the top-level status for all five TC8 test groups.

.. list-table::
   :header-rows: 1
   :widths: 32 8 9 10 41

   * - TC8 Group
     - Total
     - ✅ Tested
     - ⚠ Can add
     - Infrastructure needed
   * - SOMEIPSRV Protocol (§5.1.5)
     - 93
     - 93
     - 0
     - **N/A** — all wire-level tests complete
   * - ETS SD Protocol (§5.1.6 SD)
     - 74
     - 60
     - 0
     - **14 tests blocked — ETS application required**
       (ETS_089/096/097/103/146–151/164/166–168 require ETS C++ application)
   * - ETS Robustness (§5.1.6 robustness)
     - 14
     - 14
     - 0
     - **N/A** — all tests complete
   * - ETS Serialization / Echo (§5.1.6 echo)
     - 44
     - 0
     - 0
     - **ETS application + gatewayd** — see `ETS Application Gap`_
   * - ETS Client / Control (§5.1.6 client)
     - 5
     - 3
     - 0
     - 2 of 5 require ETS control methods — see `ETS Application Gap`_

**Key points:**

- The first three groups (181 specification items total) need only ``someipd``
  and the existing pytest framework. 167 of these items have
  passing tests; 14 ETS SD items remain blocked pending the ETS C++ application
  (see `ETS Application Gap`_).
- The last two groups (49 tests total) are **blocked**. They cannot be
  written until a C++ ETS test application is implemented. See
  `ETS Application Gap`_ for what is needed.

Current Implementation
^^^^^^^^^^^^^^^^^^^^^^

The test suite contains **183 test functions** across 10 pytest modules.

.. rubric:: Implemented Test Modules

.. list-table::
   :header-rows: 1
   :widths: 30 10 60

   * - Module
     - Tests
     - TC8 Coverage
   * - ``test_service_discovery.py``
     - 38
     - TC8-SD-001 through SD-008, SD-011, SD-013, SD-014;
       SOMEIPSRV_SD_MESSAGE_01–06/14–19; SD_BEHAVIOR_03/04;
       all ETS SD lifecycle tests
   * - ``test_sd_phases_timing.py``
     - 2
     - TC8-SD-009, SD-010
   * - ``test_sd_reboot.py``
     - 4
     - TC8-SD-012 (reboot flag + session ID reset)
   * - ``test_sd_format_compliance.py``
     - 43
     - FORMAT_01–07/09–28 (all SD header and entry fields);
       OPTIONS_01–06/08–14 (IPv4 endpoint + multicast options);
       SD_MESSAGE_07–09/11 (OfferService and Subscribe entry raw fields)
   * - ``test_sd_robustness.py``
     - 31
     - Malformed SD entry and option handling; SD framing errors;
       subscribe edge cases (ETS robustness group)
   * - ``test_sd_client.py``
     - 5
     - ETS_081/082/084 (SD client stop-subscribe, reboot detection)
   * - ``test_someip_message_format.py``
     - 42
     - TC8-MSG-001 through MSG-008;
       SOMEIPSRV_RPC_01/02/05–10/17–20;
       SOMEIPSRV_OPTIONS_15 (TCP transport binding);
       SOMEIPSRV_BASIC_01–03; SOMEIPSRV_ONWIRE_01/02/04/06/11;
       ETS_004/054/059/061/075;
       SOMEIP_ETS_068 (unaligned TCP), SOMEIP_ETS_069 (unaligned UDP)
   * - ``test_event_notification.py``
     - 9
     - TC8-EVT-001 through EVT-006; SOMEIPSRV_RPC_17 (TCP notification);
       SOMEIPSRV_RPC_15 (cyclic rate); SOMEIPSRV_RPC_16 (on-change notification)
   * - ``test_field_conformance.py``
     - 6
     - TC8-FLD-001 through FLD-004; SOMEIPSRV_RPC_17 (TCP field GET/SET)
   * - ``test_multi_service.py``
     - 3
     - SOMEIPSRV_RPC_13 (multi-service config validity);
       SOMEIPSRV_RPC_14 (instance port isolation)

All tests use ``someipd`` in ``--tc8-standalone`` mode as the DUT, exercised
via raw UDP and TCP sockets from pytest. No ``gatewayd`` or ``mw::com``
application is involved.

SOME/IP Server Tests (SOMEIPSRV_*, 93 items)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These 93 tests check the SOME/IP wire protocol at the byte level. The DUT is
``someipd`` in standalone mode. Each test sends a raw UDP or TCP packet and
checks the DUT's response. **No C++ application code or gatewayd is needed.**

The table below uses these status labels:

- **Complete** — every specification item in this category has a passing test.
- **Near-complete** — one or two items do not yet have a test, but they can
  be added using the existing framework. No new software is needed.
- **Complete (loopback skip)** — all tests are written and pass on a
  non-loopback interface. Tests that require vsomeip to include
  ``IPv4MulticastOption`` in SD messages skip automatically on loopback
  (see `Network Configurations`_).

.. rubric:: SOMEIPSRV Coverage Mapping

.. list-table::
   :header-rows: 1
   :widths: 24 7 8 17 44

   * - TC8 Category (Section)
     - Total
     - Written
     - Status
     - Notes
   * - SD Message Format (5.1.5.1)
     - 27
     - 27
     - **Complete**
     - All SD SOME/IP header fields (Client ID, Session ID, Protocol Version,
       Interface Version, Message Type, Return Code, Reboot flag, Unicast
       flag, Reserved) and all OfferService and SubscribeAck entry fields
       (FORMAT_01 through FORMAT_28) have dedicated byte-level assertions in
       ``test_sd_format_compliance.py``.
   * - SD Options Array (5.1.5.2)
     - 15
     - 15
     - **Complete** (7 skip in CI)
     - IPv4 Endpoint Option (OPTIONS_01–07), IPv4 Multicast Option
       (OPTIONS_08–14), and TCP Endpoint Option (OPTIONS_15) are all tested.
       The 7 multicast sub-field tests (OPTIONS_08–14) skip on loopback
       because vsomeip 3.6.1 does not include ``IPv4MulticastOption`` in
       SubscribeEventgroupAck when bound to a loopback address.  They run
       and pass on a non-loopback interface (see `Network Configurations`_).
   * - SD Message Entries (5.1.5.3)
     - 17
     - 17
     - **Complete**
     - Tested: FindService responses (SD_MESSAGE_01–06), OfferService raw
       entry fields including entry Type byte and both option-run fields
       (SD_MESSAGE_07–09), Subscribe request entry Type byte
       (SD_MESSAGE_11), SubscribeAck entry (SD_MESSAGE_13), NAck conditions
       (SD_MESSAGE_14–19), and Stop Subscribe raw entry format
       (SD_MESSAGE_12). All items covered.
   * - SD Communication Behavior (5.1.5.4)
     - 4
     - 4
     - **Complete**
     - Repetition phase doubling (SD_BEHAVIOR_01), Main Phase cyclic offers
       (SD_BEHAVIOR_02), and FindService response timing (SD_BEHAVIOR_03/04
       — wall-clock assertions checking the DUT responds within
       ``request_response_delay * 1.5``) are all covered.
       StopSubscribe behavior (SD_BEHAVIOR_06) is covered by TC8-SD-008.
       SD_BEHAVIOR_05 (client reaction to StopOffer) does not apply: the DUT
       is a server only and has no active client subscriptions to cancel.
   * - Basic Service Identifiers (5.1.5.5)
     - 3
     - 3
     - **Complete**
     - Service ID (BASIC_01), Instance ID (BASIC_02), and event notification
       method ID bit — bit 15 = 1 (BASIC_03) — are all verified. Note:
       vsomeip 3.6.1 fails BASIC_03 (sends a RESPONSE to event-ID messages).
       See `Known SOME/IP Stack Limitations`_.
   * - On-Wire Format (5.1.5.6)
     - 10
     - 10
     - **Complete**
     - Protocol version, message type, request/response ID echo, interface
       version, return codes, and error responses for unknown service or
       method are all verified (ONWIRE_01–07/10–12) in
       ``test_someip_message_format.py``.
   * - Remote Procedure Call (5.1.5.7)
     - 17
     - 17
     - **Complete**
     - Tested: TCP request/response (RPC_01/02), Fire-and-Forget
       (RPC_04/05), return code handling (RPC_06–10), field getter/setter
       (RPC_03/11), multiple service instances (RPC_13/14), cyclic
       notification rate (RPC_15), on-change-only notification (RPC_16),
       TCP event and field notification (RPC_17), error header echo
       (RPC_18/19/20). All items covered.

**Summary: All 93 SOMEIPSRV items have passing tests.**

ETS Tests (SOMEIP_ETS_*, 137 items)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ETS test cases split into two tracks based on what infrastructure they
need.

.. rubric:: Track A — Wire-level tests (88 items, pytest)

These tests check the SOME/IP wire protocol directly. They use ``someipd``
in standalone mode and send raw packets — exactly the same setup as the
SOMEIPSRV tests above. All wire-level ETS tests are now implemented; 14
tests in the ETS SD Protocol group remain blocked pending the ETS C++
application.

**ETS SD Protocol (74 items)**

This group covers Service Discovery at the wire level: FindService,
SubscribeEventgroup with various option types, NAck conditions, session ID
behavior, TTL expiry, reboot detection, and multicast/unicast interactions.

*Status: 60 of 74 implemented (14 blocked — require ETS application).*

All wire-level ETS SD tests that can run without the ETS C++ application
are now implemented. The 60 implemented tests cover session ID behavior,
FindService responses, subscribe edge cases, malformed SD entries and
options, TTL expiry, reboot detection, and multicast/unicast interactions.

Implemented examples:

- ``SOMEIP_ETS_088`` — two subscribes with the same session ID
- ``SOMEIP_ETS_091`` — session ID increments correctly
- ``SOMEIP_ETS_092`` — TTL=0 stop-subscribe
- ``SOMEIP_ETS_120`` — subscribe endpoint IP matches tester
- ``SOMEIP_ETS_111–142`` — malformed SD entries and options (robustness)
- ``SOMEIP_ETS_081/082/084`` — SD client stop-subscribe, reboot detection

.. rubric:: ETS SD Protocol — Blocked Tests (14 items, require ETS application)

The following 14 ETS SD test cases cannot be implemented without the ETS
C++ application:

- ``SOMEIP_ETS_089`` — ``suspendInterface`` control method required
- ``SOMEIP_ETS_096`` — TCP connection prerequisite for subscription (needs
  ETS app for TCP server)
- ``SOMEIP_ETS_097`` — TCP reconnection recovery (needs ETS app for TCP
  server)
- ``SOMEIP_ETS_103`` — ``SD_ClientServiceGetLastValueOfEventTCP`` (TCP
  event delivery, needs ETS app)
- ``SOMEIP_ETS_146`` — ``resetInterface`` control method required
- ``SOMEIP_ETS_147–151`` — ``triggerEvent*`` methods required (event push
  triggers)
- ``SOMEIP_ETS_164`` — ``suspendInterface`` control method required
- ``SOMEIP_ETS_166–168`` — ``TestField*`` methods required (field
  read/write via ETS app)

These are tracked in `ETS Application Gap`_ and will be unblocked when the
ETS C++ application is implemented.

**ETS Robustness (14 items)**

These tests send wrong protocol versions, wrong message types, wrong IDs,
truncated messages, oversized messages, and unaligned packets.

*Status: 14 of 14 implemented.*

All implemented:

- ``SOMEIP_ETS_068`` — unaligned SOME/IP messages over TCP (TC8-TCP-009 in
  ``test_someip_message_format.py``)
- ``SOMEIP_ETS_069`` — unaligned SOME/IP messages over UDP (TC8-UDP-001)
- ``SOMEIP_ETS_074/075/076/077/078`` — wrong interface version, message
  type, method ID, service ID, protocol version
- ``SOMEIP_ETS_054/055`` — length field zero or less than 8 bytes
- ``SOMEIP_ETS_004`` — burst of 10 sequential requests

.. rubric:: Track B — Tests requiring an ETS application (49 items)

.. _ETS Application Gap:

These tests **cannot run yet** because they require a C++ test application
that does not exist. The tests cannot be written until that application is
built. This is the only infrastructure gap for ETS tests.

**What is the ETS application?**

It is a small C++ program (a ``score::mw::com`` Skeleton) that implements
the TC8 service interface defined in Section 5.1.4 of the specification.
The planned location is ``tests/tc8_conformance/application/`` (the
directory structure and README are already in place, but no code exists yet).
It must expose:

- *Echo methods* — receive a value and return it unchanged
  (``echoUINT8``, ``echoUINT8Array``, ``echoUTF8DYNAMIC``, ``echoUNION``,
  and ~40 others). These let the tester verify that the full pipeline
  (mw::com Skeleton → gatewayd → someipd → network) serializes every
  SOME/IP data type correctly.
- *Event triggers* — fire an event on demand
  (``triggerEventUINT8``, ``triggerEventUINT8Reliable``, etc.)
- *Field accessors* — getter, setter, and notifier for TC8 test fields
- *Control methods* — ``resetInterface``, ``suspendInterface``,
  ``clientServiceActivate`` / ``clientServiceDeactivate``

**ETS Serialization / Echo (44 items)** ``SOMEIP_ETS_001–053, 063–073``

The tester sends an echo request with a specific data value. The DUT must
return the same value through the full pipeline. This validates the
**Payload Transformation** component inside ``gatewayd``.

*Status: 0 of 44 implemented.* These tests cannot be written until both the
ETS application and the Payload Transformation component in gatewayd exist
and are working correctly.

Data types covered by echo tests: UINT8, INT8, INT64, FLOAT64, arrays
(static and dynamic, 1D and 2D), strings (UTF-8 and UTF-16, fixed and
dynamic length), unions, enums, bitfields, E2E-protected messages, and
common data type combinations.

**ETS Client / Control (5 items)**

Three of these (``SOMEIP_ETS_081/082/084``) are already implemented in
``test_sd_client.py`` because they only need wire-level SD messages. The
remaining two (``SOMEIP_ETS_089/164``) use ``resetInterface`` and
``suspendInterface`` control methods, which require the ETS application.

*Status: 3 of 5 implemented; 2 blocked on ETS application.*

Test Framework Suitability
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. rubric:: Framework Assessment per TC8 Group

.. list-table::
   :header-rows: 1
   :widths: 28 8 24 40

   * - TC8 Test Group
     - Count
     - Framework needed
     - Current status
   * - SOMEIPSRV Protocol (all)
     - 93
     - pytest
     - ✅ **Complete** — all 93 tests written and passing.
   * - ETS SD Protocol
     - 74
     - pytest
     - ✅ **Complete** — all 60 wire-level tests written and passing.
       14 tests blocked on ETS application (see `ETS Application Gap`_).
   * - ETS Robustness
     - 14
     - pytest
     - ✅ **Complete** — all 14 tests written and passing.
   * - ETS Serialization / Echo
     - 44
     - ETS application + gatewayd + pytest
     - **0 of 44 implemented.** Blocked — ETS application and Payload
       Transformation in gatewayd do not exist yet.
   * - ETS Client / Control
     - 5
     - 3 use pytest; 2 need ETS application
     - **3 of 5 implemented** (ETS_081/082/084 in ``test_sd_client.py``).
       2 tests (ETS_089/164) blocked on ETS application.

**Framework recommendation:**

For all tests, pytest is the test framework. Wire-level tests run entirely
within the pytest process. Application-level tests extend ``conftest.py``
with a subprocess fixture that starts the ETS application, ``gatewayd``,
and ``someipd`` in order — the same ``subprocess.Popen`` pattern used for
the standalone ``someipd`` fixture. Adopt S-CORE ITF if multi-node
isolation or structured CI reporting becomes necessary.

What is Needed to Reach 100% Coverage
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The table below lists the remaining actions in priority order.

.. list-table::
   :header-rows: 1
   :widths: 5 30 11 54

   * - #
     - Action
     - Unlocks
     - Details
   * - 1
     - ✅ DONE — Write missing wire-level tests
     - 21 tests added
     - All 21 missing wire-level tests have been implemented (21 new test
       functions added in this milestone). SD_MESSAGE_12, RPC_15, RPC_16,
       all ETS SD Protocol wire-level tests, and all ETS Robustness tests
       are now written and passing.
   * - 2
     - Implement the ETS application (mw::com Skeleton)
     - 49 tests
     - Write the C++ service application in
       ``tests/tc8_conformance/application/``. The directory structure and
       README are already in place. The application must implement all echo
       methods (``echoUINT8``, ``echoUINT8Array``, ``echoUTF8DYNAMIC``, and
       ~40 others), event triggers, field accessors, and control methods
       (``resetInterface``, ``suspendInterface``,
       ``clientServiceActivate``).
   * - 3
     - Verify Payload Transformation in gatewayd
     - 44 tests (same as action 2)
     - Serialization echo tests pass only when gatewayd correctly
       serializes and deserializes all TC8 data types through the full
       pipeline. Verify each type: UINT8/INT8/FLOAT64, static and dynamic
       arrays, UTF-8 and UTF-16 strings, unions, enums, bitfields, and
       common data type combinations.
   * - 4
     - Add ETS process orchestration to conftest.py
     - 49 tests (same as action 2)
     - Add a pytest fixture that starts the ETS application, ``gatewayd``,
       and ``someipd`` in order and tears them down after the test. A simple
       ``subprocess.Popen`` fixture is sufficient. Adopt S-CORE ITF later
       if multi-node isolation is needed.
   * - 5
     - Assess E2E protection support
     - 2 tests
     - ``SOMEIP_ETS_034`` (echoUINT8E2E) and ``SOMEIP_ETS_149``
       (triggerEventUINT8E2E) require E2E middleware integration. Assess
       whether mw::com and gatewayd support E2E protection and configure it
       if needed.

Transport Layer Tests — Status
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following ETS test cases involve transport layer scenarios.

.. list-table::
   :header-rows: 1
   :widths: 20 38 27 15

   * - Spec ID
     - Title
     - TCP Scenario
     - Status
   * - SOMEIP_ETS_035
     - echoUINT8RELIABLE
     - Request/response via TCP
     - Blocked — needs ETS app
   * - SOMEIP_ETS_037
     - echoUINT8RELIABLE_client_closes_TCP_connection_automatically
     - TCP lifecycle persistence
     - Blocked — needs ETS app
   * - SOMEIP_ETS_068
     - Unaligned_SOMEIP_Messages_overTCP
     - Multiple SOME/IP messages in one TCP packet
     - ✅ **Implemented** — TC8-TCP-009 in ``test_someip_message_format.py``
   * - SOMEIP_ETS_069
     - Unaligned_SOMEIP_Messages_overUDP
     - Multiple SOME/IP messages in one UDP datagram
     - ✅ **Implemented** — TC8-UDP-001 in ``test_someip_message_format.py``
   * - SOMEIP_ETS_086
     - Eventgroup_EventsAndFieldsAll_2_TCP
     - TCP eventgroup with initial field delivery
     - Blocked — needs ETS app
   * - SOMEIP_ETS_096
     - SD_Check_TCP_Connection_before_SubscribeEventgroup
     - TCP prerequisite for subscription
     - Blocked — needs ETS app
   * - SOMEIP_ETS_097
     - SD_Client_restarts_tcp_connection
     - TCP reconnection recovery
     - Blocked — needs ETS app

``SOMEIP_ETS_068`` and ``SOMEIP_ETS_069`` are the only transport layer tests
that can be tested at the wire level (no application needed). Both are
implemented. The TCP helper functions ``tcp_send_concatenated()`` and
``tcp_receive_n_responses()`` live in ``helpers/tcp_helpers.py``; the UDP
equivalents ``udp_send_concatenated()`` and ``udp_receive_responses()`` live
in ``helpers/udp_helpers.py``. All remaining TCP tests require the ETS
application and Payload Transformation in gatewayd.

Known SOME/IP Stack Limitations
---------------------------------

The following table records the known limitations of **vsomeip 3.6.1**
against the OA TC8 v3.0 specification.  This table must be reviewed and
updated whenever the SOME/IP stack version changes.

Each test listed here is decorated with ``@pytest.mark.xfail(strict=True)``
so that CI passes despite the known non-conformance.  ``strict=True`` ensures
that if the limitation is fixed in a future stack version, the unexpected pass
(XPASS) will cause CI to fail, prompting removal of the marker.

.. list-table::
   :header-rows: 1
   :widths: 25 35 30 10

   * - OA Spec Reference
     - Specification Requirement
     - vsomeip 3.6.1 Actual Behaviour
     - Test Result
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_19
     - SubscribeEventgroup with reserved bits set in the entry MUST be
       responded to with a NAck (SubscribeEventgroupAck with TTL = 0).
     - Sends a positive SubscribeEventgroupAck (TTL > 0) regardless of
       reserved bits.
     - **XFAIL** —
       ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_19_reserved_field_set``
   * - §5.1.5.5 — SOMEIPSRV_BASIC_03
     - When the DUT receives a message with method_id bit 15 = 1 (event
       notification ID), it MUST NOT send a RESPONSE (message_type 0x80).
     - Sends a RESPONSE (message_type 0x80) for event-ID messages even
       though the spec prohibits it.
     - **XFAIL** —
       ``test_someip_message_format::TestSomeipBasicIdentifiers::test_basic_03_event_method_id_no_response``
   * - §5.1.5.7 — SOMEIPSRV_RPC_08
     - The DUT MUST NOT send a reply to a REQUEST message that already
       carries a non-zero return code.
     - Processes the REQUEST normally and sends a RESPONSE, ignoring the
       return code field.
     - **XFAIL** —
       ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_rpc_08_request_with_error_return_code_no_reply``
