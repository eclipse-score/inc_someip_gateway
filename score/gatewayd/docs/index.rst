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

.. _gatewayd:

gatewayd
########

Abstract
========

The SOME/IP Gateway Daemon (``gatewayd``) is the ``ASIL_B`` gateway process in the S-CORE
framework that bridges local ``mw::com`` (LoLa) service instances with the ``QM`` ``someipd``
network daemon across the safety boundary. Running with zero dependency on ``vsomeip``, it
connects ``ASIL_B`` and ``QM`` local applications to ``someipd`` over ``gateway_ipc_binding``,
framing or stripping the 16-byte SOME/IP header and serializing or deserializing payloads via
the ``score_com_serializer`` C plugin ABI:

- ``gatewayd`` (``score/gatewayd/main.cpp``): Daemon entry point that loads the gateway configuration and ``mw::com`` manifest, initializes the serializer plugin, LoLa runtime, and in-process ``socom::Runtime``, connects the ``gateway_ipc_binding`` client to ``someipd``, waits for ``is_connected()``, and starts all configured service instances.
- ``LocalServiceInstance``: Application-to-network adapter that discovers locally provided ``mw::com`` services via ``GenericProxy``, frames the 16-byte SOME/IP header and serializes application event samples into shared memory, and publishes them to ``someipd`` through a ``socom`` server connector.
- ``RemoteServiceInstance``: Network-to-application adapter that offers a local ``mw::com::GenericSkeleton`` on the ECU, subscribes to remote SOME/IP events from ``someipd`` through a ``socom`` client connector, strips the 16-byte SOME/IP header, and publishes deserialized samples to local consumers.


.. comp:: SOME/IP Gateway Daemon
   :id: comp__gatewayd
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway
   :implements: real_arc_int_op__gatewayd_arch__local_svc, real_arc_int_op__gatewayd_arch__remote_svc
   :uses: real_arc_int_op__gib_arch__create_client, real_arc_int_op__gib_arch__is_connected, real_arc_int_op__socom_design__make_client, real_arc_int_op__socom_design__make_server, real_arc_int_op__socom_design__sub_event, real_arc_int_op__socom_design__enable_srv, real_arc_int_op__socom_design__alloc_payload, real_arc_int_op__socom_design__update_event, real_arc_int_op__socom_design__payload_span

   Connects :need:`comp__someipd` to local applications via ``mw::com`` (LoLa) IPC, framing and serializing application samples into full SOME/IP messages for local services and stripping headers and deserializing SOME/IP payloads for remote services within an ``ASIL_B`` process boundary.

.. toctree::
   :maxdepth: 2

   architecture/index
