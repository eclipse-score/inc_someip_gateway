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

.. _socom:

SOCom
#####

Abstract
========

The SOCom (Service-Oriented Communication) library (``score::socom``) provides the in-process
service-oriented runtime and connector abstraction for SOME/IP communication in the S-CORE
framework. It decouples service consumers and providers from concrete SOME/IP network stack and
IPC bridge implementations through its ``Runtime``, ``Client_connector``,
``Disabled_server_connector`` / ``Enabled_server_connector``, and zero-copy ``Payload`` /
``Writable_payload`` abstractions.

.. comp:: SOCom
   :id: comp__socom
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :belongs_to: feat__someip_gateway
   :implements: real_arc_int_op__socom_design__make_client, real_arc_int_op__socom_design__make_server, real_arc_int_op__socom_design__sub_event, real_arc_int_op__socom_design__call_method, real_arc_int_op__socom_design__enable_srv, real_arc_int_op__socom_design__alloc_payload, real_arc_int_op__socom_design__update_event, real_arc_int_op__socom_design__payload_span

   In-process SOME/IP service-oriented runtime and connector abstraction library.

.. toctree::
   :maxdepth: 2

   design/index
