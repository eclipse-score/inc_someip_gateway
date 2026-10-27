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

SOCom Component Requirements
============================

These requirements cover behavior provided by the SOCom connector abstraction.
They do not restate the general ``mw::com`` public API contract.

.. comp_req:: SOCom service identity and version compatibility
   :id: comp_req__socom__identity_and_version
   :status: valid
   :version: 1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__service_instance, feat_req__com__service_discovery, feat_req__com__versioning
   :satisfied_by: comp__socom

   A SOCom client connector shall become available only for a server connector
   with the requested service interface identifier and service instance. The
   interface major version shall match, and the server minor version shall be
   compatible with the client minor version.

.. comp_req:: SOCom event delivery to subscribed clients
   :id: comp_req__socom__event_delivery
   :status: valid
   :version: 1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__socom

   When a SOCom client subscribes to an event, updates published by the
   corresponding server connector shall be delivered to that client with the
   published payload.
