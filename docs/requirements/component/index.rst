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

Component Requirements
======================

Component-level requirements for the SOME/IP Gateway. Each component
requirement derives from a feature requirement via ``:satisfies:`` and
is scoped to a specific component (``gatewayd``, ``someipd``, or
``network_service``).

Component requirement IDs follow the format:
``comp_req__<component>__<title_snake_case>``

Assumption of Use (AoU) requirements use:
``aou_req__<component>__<title_snake_case>``

.. toctree::
   :maxdepth: 1

   socom
   gateway_ipc_binding
   gatewayd
