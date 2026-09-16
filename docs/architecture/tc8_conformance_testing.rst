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

- **Protocol Conformance**: tests the production ``someipd`` binary at the wire level using
  `scapy <https://scapy.net/>`_ as the packet serializer and parser. No application processes
  are needed.

- **Application Level Tests**: tests the full gateway path from mw::com client
  through ``gatewayd`` and ``someipd`` to the network, using C++ apps
  built on ``score::mw::com``. These tests are stack-agnostic.

Both scopes live under ``tests/tc8_conformance/`` and share the ``tc8`` and
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
     [someipd] as L1DUT
     [gatewayd] as L1GW
     L1Test -down-> L1DUT : raw SOME/IP\nUDP / TCP
     L1DUT -right-> L1GW : IPC (idles)
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
level. Tests send raw SOME/IP messages and verify responses against the TC8
specification.

DUT Binary
^^^^^^^^^^

The protocol conformance DUT is the production ``someipd`` binary.  It uses
vsomeip directly for all SOME/IP network I/O and service discovery.
``someipd`` is QM only and is not part of the ASIL-B safety path.

At startup, ``someipd`` reads a FlatBuffer binary config (``-c`` flag) that
declares the service ID, instance ID, version, and events to offer. The binary
initialises a vsomeip application and calls ``offer_service()`` for each entry.
For TC8, the config is ``tc8_someipd_config.bin``, generated at build time from
``tests/tc8_conformance/config/tc8_someipd_config.json``.

``gatewayd`` is started alongside ``someipd`` as a companion process.  In the
SD-only conformance tests ``gatewayd`` idles (its FlatBuffer config declares no
service types), but the IPC handshake between ``someipd`` and ``gatewayd`` must
complete before the test proceeds.  ``gatewayd`` becomes active only in ETS
end-to-end tests where a mw::com application offers or consumes the TC8 service.

``tc8_itf_conftest.py`` launches ``someipd`` first (it becomes the vsomeip
routing manager), waits for an OfferService multicast, then starts ``gatewayd``.
Both processes are force-killed (``pkill -9``) during fixture teardown.

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
    satisfying this does not require a fixed port, it requires only that
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

Application Level Tests
-----------------------

Application level tests verify the full gateway pipeline end to end.
A **service** (mw::com Skeleton) and **client** (mw::com Proxy) communicate
through ``gatewayd`` and ``someipd``. Because both apps use the mw::com API
only, the same test code works with any SOME/IP binding.

.. note::

   Application level tests are planned. See
   ``tests/tc8_conformance/application/README.md`` for the intended scope.

Planned Topology
^^^^^^^^^^^^^^^^

The application level test topology matches the "Application-Level Tests"
package shown in the diagram under `Test Scope Overview`_.

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

The application level test design introduces four planned components.
The **Enhanced Testability Service** (**ETS**) and **Enhanced Testability
Client** (**ETC**) implement the TC8 service interface defined in OA TC8
§6.1.4, while the **Test Orchestrator** and **Process Orchestrator** manage
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

``someipd`` and ``gatewayd`` run as the DUT in the QEMU guest. The Python
socket-based tester and pytest run on the host side. ``tc8_itf_conftest.py``
manages DUT lifecycle on the QEMU guest. ``TC8_DUT_IP`` addresses the QEMU
guest and ``TC8_TESTER_IP`` addresses the host TAP interface.
