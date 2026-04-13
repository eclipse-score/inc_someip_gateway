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
   scale max 800 width
   skinparam packageStyle rectangle

   package "Protocol Conformance" {
     [pytest] as L1Test
     [someipd (standalone)] as L1DUT
     L1Test -down-> L1DUT : raw SOME/IP\nUDP / TCP
   }

   package "Application-Level Tests" {
     [pytest\norchestrator] as L2Orch

     [TC8 Service\n(mw::com Skeleton)] as L2Svc
     [gatewayd] as L2GW1
     [someipd] as L2SD1

     [someipd] as L2SD2
     [gatewayd] as L2GW2
     [TC8 Client\n(mw::com Proxy)] as L2Cli

     L2Orch .down.> L2Svc
     L2Orch .down.> L2GW1
     L2Orch .down.> L2SD1
     L2Orch .down.> L2SD2
     L2Orch .down.> L2GW2
     L2Orch .down.> L2Cli

     L2Svc -right-> L2GW1 : LoLa IPC
     L2GW1 -right-> L2SD1 : LoLa IPC
     L2SD1 -right-> L2SD2 : SOME/IP\nUDP / TCP
     L2SD2 -right-> L2GW2 : LoLa IPC
     L2GW2 -right-> L2Cli : LoLa IPC
   }

   L1DUT -[hidden]down-> L2Orch
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

For the per-target port matrix, see ``tests/tc8_conformance/README.md``.

Test Module Structure
^^^^^^^^^^^^^^^^^^^^^

Each TC8 area has a test module (pytest) and one or more helper modules.
The diagrams below show the dependencies grouped by TC8 domain.
Blue boxes represent test modules and green boxes represent shared helper
modules. Dashed arrows indicate internal helper-to-helper dependencies.

Service Discovery (SD)
~~~~~~~~~~~~~~~~~~~~~~~~

The Service Discovery tests (TC8-SD) verify SOME/IP-SD offer announcements,
find/subscribe responses, SD phase timing, byte-level SD field values,
malformed packet robustness, and SD client lifecycle.

.. uml::

   @startuml
   !theme plain
   scale max 800 width
   skinparam component {
     BackgroundColor<<test>> #E3F2FD
     BorderColor<<test>> #1565C0
     BackgroundColor<<helper>> #E8F5E9
     BorderColor<<helper>> #2E7D32
   }

   title Service Discovery — Test Module Dependencies

   [test_service_discovery] <<test>>
   [test_sd_phases_timing] <<test>>
   [test_sd_reboot] <<test>>
   [test_sd_format_compliance] <<test>>
   [test_sd_robustness] <<test>>
   [test_sd_client] <<test>>

   [sd_helpers] <<helper>>
   [sd_sender] <<helper>>
   [sd_malformed] <<helper>>
   [someip_assertions] <<helper>>
   [timing] <<helper>>

   test_service_discovery --> sd_helpers
   test_service_discovery --> sd_sender
   test_service_discovery --> someip_assertions
   test_service_discovery --> timing
   test_sd_phases_timing --> timing
   test_sd_phases_timing --> sd_helpers
   test_sd_reboot --> sd_helpers
   test_sd_format_compliance --> sd_helpers
   test_sd_robustness --> sd_malformed
   test_sd_robustness --> sd_helpers
   test_sd_client --> sd_helpers
   test_sd_client --> sd_sender

   timing ..> sd_helpers : <<uses>>
   @enduml

Message Format, Events, Fields, and TCP Transport (MSG / EVT / FLD / TCP)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These tests cover message format (TC8-MSG), event notification (TC8-EVT),
field access (TC8-FLD), and TCP transport binding. Domain-specific helpers
handle packet construction, subscription workflows, field get/set operations,
and TCP stream framing.

.. uml::

   @startuml
   !theme plain
   scale max 800 width
   skinparam component {
     BackgroundColor<<test>> #E3F2FD
     BorderColor<<test>> #1565C0
     BackgroundColor<<helper>> #E8F5E9
     BorderColor<<helper>> #2E7D32
   }

   title Message / Event / Field / TCP — Test Module Dependencies

   [test_someip_message_format] <<test>>
   [test_event_notification] <<test>>
   [test_field_conformance] <<test>>

   [message_builder] <<helper>>
   [someip_assertions] <<helper>>
   [sd_helpers] <<helper>>
   [sd_sender] <<helper>>
   [event_helpers] <<helper>>
   [field_helpers] <<helper>>
   [tcp_helpers] <<helper>>
   [udp_helpers] <<helper>>

   test_someip_message_format --> message_builder
   test_someip_message_format --> someip_assertions
   test_someip_message_format --> sd_helpers
   test_someip_message_format --> tcp_helpers
   test_someip_message_format --> udp_helpers
   test_event_notification --> event_helpers
   test_event_notification --> sd_helpers
   test_event_notification --> sd_sender
   test_event_notification --> tcp_helpers
   test_field_conformance --> field_helpers
   test_field_conformance --> event_helpers
   test_field_conformance --> sd_helpers

   event_helpers ..> sd_sender : <<uses>>
   field_helpers ..> message_builder : <<uses>>
   field_helpers ..> tcp_helpers : <<uses>>
   @enduml

Multi-service and Multi-instance
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``test_multi_service.py`` verifies that ``someipd`` correctly handles vsomeip
configurations that declare multiple service entries, each advertising its own
distinct UDP port in the SD endpoint option.

.. uml::

   @startuml
   !theme plain
   scale max 800 width
   skinparam component {
     BackgroundColor<<test>> #E3F2FD
     BorderColor<<test>> #1565C0
     BackgroundColor<<helper>> #E8F5E9
     BorderColor<<helper>> #2E7D32
   }

   title Multi-service / Multi-instance — Test Module Dependencies

   [test_multi_service] <<test>>

   [sd_helpers] <<helper>>
   [sd_sender] <<helper>>

   test_multi_service --> sd_helpers
   test_multi_service --> sd_sender
   @enduml

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
   scale max 800 width

   node "Host" {
     [TC8 Service\n(mw::com Skeleton)] as Svc
     [gatewayd] as GW1
     [someipd] as SD1

     [someipd] as SD2
     [gatewayd] as GW2
     [TC8 Client\n(mw::com Proxy)] as Cli

     Svc -right-> GW1 : LoLa IPC
     GW1 -right-> SD1 : LoLa IPC
     SD1 -right-> SD2 : SOME/IP\nUDP / TCP
     SD2 -right-> GW2 : LoLa IPC
     GW2 -right-> Cli : LoLa IPC
   }

   [pytest\norchestrator] as Orch
   Orch .down.> Svc
   Orch .down.> GW1
   Orch .down.> SD1
   Orch .down.> SD2
   Orch .down.> GW2
   Orch .down.> Cli
   @enduml

Stack-Agnostic Design
^^^^^^^^^^^^^^^^^^^^^

The test apps depend only on ``score::mw::com``. Switching the SOME/IP stack
requires changing the deployment config, not test code.

.. uml::

   @startuml
   !theme plain
   scale max 800 width

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

The application-level test design introduces four planned components.
The **Enhanced Testability Service** (**ETS**) and **Enhanced Testability
Client** (**ETC**) implement the TC8 service interface defined in OA TC8
§5.1.4, while the **Test Orchestrator** and **Process Orchestrator** manage
test and process lifecycle.

.. uml::

   @startuml
   !theme plain
   scale max 800 width
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
