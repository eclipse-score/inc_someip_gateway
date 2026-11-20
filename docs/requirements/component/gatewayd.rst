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

.. comp_req:: gatewayd publishes local event samples as SOME/IP notifications
   :id: comp_req__gatewayd__forwards_local_events
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gatewayd

   When gatewayd receives a new sample of a bridged local event, it shall
   publish a SOME/IP notification for it as an update of the matching event on
   the SOCom server connector of that service.

   The published payload is a 16-byte SOME/IP header followed by the serialized
   sample, with no bytes after it. The header holds the configured service ID,
   the configured event ID as method ID, client ID ``0xFFFF``, session ID ``0``,
   protocol version ``1``, the service major version as interface version,
   message type NOTIFICATION (``0x02``) and return code ``0``. This requirement
   does not cover the length field.

.. comp_req:: gatewayd offers remote events locally
   :id: comp_req__gatewayd__offers_remote_events
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gatewayd

   When someipd passes a SOME/IP event message for a bridged remote service,
   gatewayd shall deserialize it and send the result as a sample of the
   matching local service event.

.. comp_req:: gatewayd drops event samples it cannot process
   :id: comp_req__gatewayd__drops_failed_samples
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gatewayd

   If a step fails while gatewayd handles an event sample, gatewayd shall drop
   that sample without forwarding it. The steps are: finding the matching
   local event, allocating the payload or sample, serializing or deserializing
   it, and sending it. If reading new samples from mw::com fails, gatewayd
   shall forward no sample from that read.

.. comp_req:: gatewayd does not create a service instance whose setup fails
   :id: comp_req__gatewayd__reports_failed_setup
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__com__event_type, feat_req__com__producer_consumer
   :satisfied_by: comp__gatewayd

   If a step fails while gatewayd sets up a bridged service instance, gatewayd
   shall report the error and shall not create that instance. The steps are:
   checking that the instance configuration is present, finding a serializer
   for each event of a remote service, starting service discovery, creating
   the mw::com proxy or skeleton, offering the skeleton, and creating the SOCom
   connector.

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
