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

Components
==========

Safety Classification Split
---------------------------

The SOME/IP Gateway architecture separates safety-qualified processing from the QM network stack across two daemon processes connected via ``mw::com`` (LoLa) shared-memory IPC:

- **ASIL_B Side (Gateway and Serialization)**:

  - :need:`comp__gatewayd` (``ASIL_B``): Bridges safety-relevant application ``mw::com`` services with the inter-daemon IPC link, enforcing End-to-End (E2E) protection over contiguous SOME/IP headers and payloads as well as bounds checks on incoming IPC samples.
  - :need:`comp__serializer` (``ASIL_B``): Serializes and deserializes application data types to and from SOME/IP wire format within ``gatewayd``.
  - :need:`comp__null_serializer` (``ASIL_B``): Passthrough serializer used by ``gatewayd`` when payloads are pre-serialized by the application.

- **QM Side (Network Daemon, IPC Binding, Abstraction, Protocol, and Configuration)**:

  - :need:`comp__socom` (``QM``): Intra-process service-oriented communication abstraction (``Runtime``, ``Client_connector``, ``Server_connector``) used inside both ``gatewayd`` and ``someipd`` to decouple service endpoints from IPC and network bindings.
  - :need:`comp__gateway_ipc_binding` (``QM``): Bridges ``score::socom`` services between ``gatewayd`` and ``someipd`` over ``score::mw::com`` (``GenericSkeleton`` / ``GenericProxy`` and ``SomeipdService``) using ``"asil-level": "QM"`` shared-memory instances.
  - :need:`comp__someipd` (``QM``): Central SOME/IP network daemon that owns the network sockets, handles SOME/IP Service Discovery, and routes messages between network peers and registered ``gatewayd`` instances.
  - :need:`comp__someip` (``QM``): SOME/IP protocol header, identifier, and error code definitions used to frame and parse SOME/IP messages.
  - :need:`comp__config` (``QM``): FlatBuffers schema (``mw_someip_config.fbs``) and JSON schema defining the gateway service and instance configuration.


..
   # Currently disabled because of issue eclipse-score/docs-as-code#580
   .. logic_arc_int:: Serializer Interface
      :id: logic_arc_int__someip__serializer
      :security: NO
      :safety: ASIL_B
      :status: valid

      The interface between the :need:`comp__gatewayd` and the serializer component.
      The gateway daemon uses this interface to serialize and deserialize SOME/IP messages.
