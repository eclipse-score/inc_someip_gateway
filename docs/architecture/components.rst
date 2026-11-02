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

  - :need:`comp__gatewayd` (``ASIL_B``): Bridges local ``mw::com`` application services with ``someipd`` over the IPC binding, framing or stripping the 16-byte SOME/IP header contiguously with the payload in shared memory (enabling future E2E protection in the ``ASIL_B`` context) and serializing or deserializing application samples. See :ref:`gatewayd_architecture`.
  - :need:`comp__serializer` (``ASIL_B``): Serializes and deserializes application data types to and from SOME/IP wire format within ``gatewayd``.
  - :need:`comp__null_serializer` (``ASIL_B``): Passthrough serializer used by ``gatewayd`` when payloads are pre-serialized by the application.

- **QM Side (Network Daemon, IPC Binding, Abstraction, Protocol, and Configuration)**:

  - :need:`comp__socom` (``QM``): In-process service-oriented communication abstraction used inside both ``gatewayd`` and ``someipd`` to decouple service endpoints from IPC and network bindings. See :ref:`software_element_socom`.
  - :need:`comp__gateway_ipc_binding` (``QM``): Bridges ``socom`` services between ``gatewayd`` and ``someipd`` over ``mw::com`` shared-memory IPC and provides daemon connectivity and runtime configuration exchange. See :ref:`gateway_ipc_binding_architecture`.
  - :need:`comp__someipd` (``QM``): Central SOME/IP network daemon that owns the network sockets, handles SOME/IP Service Discovery, and routes messages between network peers and registered ``gatewayd`` instances.
  - :need:`comp__someip` (``QM``): SOME/IP protocol header, identifier, and error code definitions used to frame and parse SOME/IP messages.
  - :need:`comp__config` (``QM``): FlatBuffers and JSON schemas defining the gateway service and instance configuration.


..
   # Currently disabled because of issue eclipse-score/docs-as-code#580
   .. logic_arc_int:: Serializer Interface
      :id: logic_arc_int__someip__serializer
      :security: NO
      :safety: ASIL_B
      :status: valid

      The interface between the :need:`comp__gatewayd` and the serializer component.
      The gateway daemon uses this interface to serialize and deserialize SOME/IP messages.
