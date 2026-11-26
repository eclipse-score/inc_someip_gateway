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

gatewayd Component Requirements
===============================

These requirements cover externally observable gateway daemon behavior.
General service-instance and versioning semantics remain owned by ``mw::com``.

.. comp_req:: gatewayd starts from runtime deployment configuration
   :id: comp_req__gatewayd__starts_from_config
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__depl_config_runtime
   :satisfied_by: comp__gatewayd

   On startup, gatewayd shall initialize the mw::com runtime from the
   supplied service-instance manifest and, for each service type declared in
   the SOME/IP configuration file, create a local bridge for every
   ``local_service_instances`` entry and a remote bridge for every
   ``remote_service_instances`` entry.

   Integrity verification of the service-instance manifest is a property of
   the mw::com runtime and is not performed by gatewayd itself; integrity
   of the SOME/IP configuration file is covered by
   :need:`aou_req__gatewayd__someip_config_integrity`.

.. comp_req:: gatewayd prints usage on request
   :id: comp_req__gatewayd__prints_usage
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Interface
   :satisfied_by: comp__gatewayd

   When started with the ``--help`` command-line argument, gatewayd shall
   print its usage text and exit with a success exit code.

.. comp_req:: gatewayd forwards local service events to the SOME/IP network
   :id: comp_req__gatewayd__forwards_local_events
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__some_ip_gateway__event_forwarding, feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gatewayd

   For every ``local_service_instances`` entry in the SOME/IP configuration,
   gatewayd shall subscribe to the ``mw::com`` skeleton offered by the local
   application, serialize each received sample via ``score_com_serializer``,
   frame it with the 16-byte SOME/IP header, and forward the framed message to
   ``someipd`` via the gateway IPC binding for transmission on the SOME/IP
   network as a notification of the configured event.

.. comp_req:: gatewayd forwards remote SOME/IP events to local mw::com subscribers
   :id: comp_req__gatewayd__forwards_remote_events
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__some_ip_gateway__event_forwarding, feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gatewayd

   For every ``remote_service_instances`` entry in the SOME/IP configuration,
   gatewayd shall offer a local ``mw::com`` skeleton and, for each event
   notification received from ``someipd`` via the gateway IPC binding, strip
   the 16-byte SOME/IP header, deserialize the remaining payload via
   ``score_com_serializer``, and publish the sample to local subscribers.

.. aou_req:: Integrity of the SOME/IP configuration file
   :id: aou_req__gatewayd__someip_config_integrity
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Non-Functional

   The integrator shall supply gatewayd with a SOME/IP configuration file
   whose integrity has been verified before the file is passed on the
   ``--configuration`` command-line argument. gatewayd consumes this file
   as-supplied and performs no additional integrity check.
