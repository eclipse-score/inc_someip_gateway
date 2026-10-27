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

Gateway IPC Binding Component Requirements
===========================================

These requirements cover the Gateway IPC Binding's control and event data
transport. They do not claim method-call transport, which is not implemented.

.. comp_req:: IPC binding connects service connectors across peers
   :id: comp_req__gib__connects_service_connectors
   :status: valid
   :version: 1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__service_discovery, feat_req__com__service_instance_names
   :satisfied_by: comp__gateway_ipc_binding

   The IPC binding shall carry the ``Service_instance`` identity of each
   offered service between peers and, once the IPC client and server are
   connected, establish a local SOCom client or server connector whose
   service-instance identifier matches the identity received from the remote
   peer.

.. comp_req:: IPC binding propagates event subscriptions
   :id: comp_req__gib__propagates_subscriptions
   :status: valid
   :version: 1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__producer_consumer
   :satisfied_by: comp__gateway_ipc_binding

   A subscription or unsubscription to a bridged SOCom event on one peer shall
   be propagated to the corresponding server connector on the other peer.

.. comp_req:: IPC binding forwards event payloads across peers
   :id: comp_req__gib__forwards_event_payloads
   :status: valid
   :version: 1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gateway_ipc_binding

   When an event is updated on a bridged SOCom server connector, the binding
   shall deliver the same payload bytes to subscribed clients on the remote
   peer.

