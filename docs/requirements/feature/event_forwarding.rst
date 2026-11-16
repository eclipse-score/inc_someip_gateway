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

Application Event Forwarding
============================

This feature requirement covers the end-to-end application-level event path
that the SOME/IP Gateway exists to provide: an event published by an
``mw::com`` application on one side of the gateway reaches a subscribing
``mw::com`` application on the other side, transported over SOME/IP by
``gatewayd`` and ``someipd``, with the same payload bytes.

It is distinct from the TC8 feature requirement (see
:doc:`/tc8_conformance/requirements`), which covers SOME/IP wire-level
conformance without application processes.

.. feat_req:: Application Event Forwarding Across the Gateway
   :id: feat_req__some_ip_gateway__event_forwarding
   :status: valid
   :version: 1
   :tags: event, forwarding, mw_com, someip, end-to-end
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   When an ``mw::com`` application offers an event via a local ``gatewayd``
   / ``someipd`` instance, and a remote SOME/IP peer subscribes to the same
   event, the SOME/IP Gateway shall deliver event samples from the
   application to the peer with the payload bytes unchanged. The symmetric
   direction holds: when a remote SOME/IP peer offers an event and a local
   ``mw::com`` application subscribes, the gateway shall deliver event
   samples from the peer to the application with the payload bytes
   unchanged.

   Scope: events only. Method calls and fields are out of scope.
