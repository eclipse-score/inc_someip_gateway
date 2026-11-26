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
that the SOME/IP Gateway provides: an event published by a local ``mw::com``
application reaches a subscribing remote SOME/IP peer via ``gatewayd`` and
``someipd``, and an event published by a remote SOME/IP peer reaches a
subscribing local ``mw::com`` application, with the payload bytes unchanged.

It is distinct from the TC8 feature requirement (see
:doc:`/tc8_conformance/requirements`), which covers SOME/IP wire-level
conformance without application processes.

.. feat_req:: Application Event Forwarding Across the Gateway
   :id: feat_req__some_ip_gateway__event_forwarding
   :status: valid
   :version: 1
   :tags: event, forwarding, mw_com, someip, end-to-end
   :derived_from: stkh_req__communication__supported_net, stkh_req__communication__safe
   :satisfied_by: feat__com_communication
   :safety: ASIL_B
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
