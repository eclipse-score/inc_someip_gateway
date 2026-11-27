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
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__service_discovery, feat_req__com__service_instance_names
   :satisfied_by: comp__gateway_ipc_binding

   When a configured service is offered on one side of the IPC connection, the
   binding shall make it available to SOCom clients on the other side, and make
   it unavailable when the service stops being offered.

.. comp_req:: IPC binding propagates event subscriptions
   :id: comp_req__gib__propagates_subscriptions
   :status: valid
   :version: 1
   :safety: ASIL_B
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
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gateway_ipc_binding

   When an event is updated on a bridged SOCom server connector, the binding
   shall deliver the same payload bytes to subscribed clients on the remote
   peer.

.. comp_req:: IPC binding rejects invalid configuration
   :id: comp_req__gib__rejects_invalid_config
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__service_discovery
   :satisfied_by: comp__gateway_ipc_binding

   If the binding is given an invalid configuration, it shall reject it and
   shall keep no service binding for it. Depending on the call, the rejection
   is an error result, a null binding, or a ``false`` reply to
   AddServiceConfiguration. The binding does not remove an mw::com deployment
   that it already added for the bridged services.

   A configuration is invalid if any of these hold:

   - An instance specifier is not a valid mw::com instance specifier, or the
     SomeipdService instance specifier of the server binding is empty.
   - When the binding creates an mw::com consumer, for a bridged service or for
     SomeipdService, its instance specifier is not deployed in mw::com.
   - A bridged service instance specifier lacks the ``bridged_`` prefix, or is
     used by more than one bridged service.
   - Two bridged services use the same service ID.
   - A bridged service has no events, an event has no name, or an event name or
     event ID is used twice.
   - An event payload size is larger than the sample length field can hold.
   - The sample count is zero for a consumer, or larger than the number of
     sample slots.
   - The configuration that gatewayd sends to someipd is larger than its fixed
     size, its event sizes do not match the provider events, or the someipd
     configuration handler rejects it.

.. comp_req:: IPC binding reports failed setup
   :id: comp_req__gib__reports_setup_failure
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__service_discovery
   :satisfied_by: comp__gateway_ipc_binding

   If an mw::com or SOCom operation fails while the binding creates or starts a
   service binding or the SomeipdService connection, the binding shall report
   the failure as follows:

   - A create, start or offer call of a service binding, of the SomeipdService
     provider or consumer, or of the server binding returns an error.
   - ``create_client`` returns a null binding.
   - AddServiceConfiguration replies ``false`` if adding the bridged services
     to mw::com or creating their service bindings fails.
   - If a service binding that someipd starts after an accepted
     AddServiceConfiguration fails to start, the binding logs the error and
     the service stays unavailable.
   - If gatewayd cannot create the SomeipdService proxy for a discovered
     someipd, or the AddServiceConfiguration call fails or is rejected, the
     binding logs the error and ``is_connected()`` returns ``false``.

   Such failures include mw::com that cannot create a skeleton or resolve an
   instance, for example because the instance is not deployed, and an mw::com
   event that is missing or whose sample size differs from the configured
   sample size.

.. comp_req:: IPC binding keeps a mismatched peer service unavailable
   :id: comp_req__gib__rejects_mismatched_peer
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__service_discovery
   :satisfied_by: comp__gateway_ipc_binding

   If a discovered peer service lacks a configured event, or has a smaller
   sample size than configured for an event, the binding shall keep the service
   unavailable to SOCom clients.
