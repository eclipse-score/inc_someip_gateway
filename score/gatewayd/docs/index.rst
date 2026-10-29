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

The ``gatewayd`` daemon bridges local ``mw::com`` (LoLa) service instances with remote SOME/IP service instances across the safety boundary. Running as an ``ASIL_B`` process with zero dependency on ``vsomeip``, ``gatewayd`` connects ``ASIL_B`` and ``QM`` local applications to the ``QM`` ``someipd`` daemon over the ``mw::com``-based ``gateway_ipc_binding``, performing SOME/IP 16-byte header framing and stripping as well as payload serialization and deserialization via the ``score_com_serializer`` C plugin ABI.


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
