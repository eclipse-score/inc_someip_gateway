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

Component Config Requirements
#############################

Functional Requirements
-----------------------

.. comp_req:: FlatBuffers gateway configuration schema
   :id: comp_req__config__flatbuffer_schema
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__config

   The gateway configuration shall be defined as a FlatBuffers schema
   (``mw_someip_config.fbs``) together with a JSON schema
   (``mw_someip_config.schema.json``) so both binary and JSON forms of
   the configuration can be validated against a single source of truth.

.. comp_req:: Service instance identity keyed by SOME/IP IDs
   :id: comp_req__config__service_instance_ids
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__config

   The configuration schema shall represent every service via a 16-bit
   ``service_id``, an 8-bit ``service_version_major`` and a 32-bit
   ``service_version_minor`` (SOME/IP wildcard ``0xFFFFFFFF`` allowed),
   and every service instance via a 16-bit ``instance_id``, so that the
   on-wire identity of every configured service and instance matches
   the SOME/IP header fields defined by the open protocol
   specification.

.. comp_req:: Separate local and remote service instance sets per service type
   :id: comp_req__config__local_remote_split
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__config

   Each ``ServiceType`` shall carry two disjoint lists,
   ``local_service_instances`` (offered from this host) and
   ``remote_service_instances`` (consumed from remote hosts), so the
   gateway can decide from configuration alone which instances to
   publish and which to subscribe to.

