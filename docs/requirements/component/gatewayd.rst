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
