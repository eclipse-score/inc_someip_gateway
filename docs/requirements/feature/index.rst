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

Feature Requirements
====================

Feature-level requirements capture project-owned capabilities and derive from
a local stakeholder requirement via ``:derived_from:`` when a local parent
exists. Each feature requirement is scoped to a specific gateway capability.
External specification prose and titles are not copied into local needs.
Where an external requirement is represented by a generated metadata-only need,
cross-reference it from the applicable local requirement; its source location
is rendered as an upstream URL. Use direct URLs only when there is no
corresponding generated need. Open SOME/IP requirements used as supporting
protocol context for TC8 are cross-referenced from
:doc:`/tc8_conformance/requirements`; OA TC8 remains the primary basis for the
conformance test scope. Reserve ``:derived_from:`` for actual parent
relationships.

Feature requirement IDs follow these formats:
``feat_req__some_ip_gateway__<title_snake_case>``
``feat_req__<scope>__<title_snake_case>``

.. note::

   Add feature requirement files to the toctree below as they are created.
   Group requirements by feature area (e.g., ``service_discovery.rst``,
   ``e2e_protection.rst``, ``acl.rst``).

.. toctree::
   :maxdepth: 1
