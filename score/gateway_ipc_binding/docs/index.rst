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

.. _gateway_ipc_binding:

Gateway IPC Binding
###################

.. comp:: Gateway IPC Binding
   :id: comp__gateway_ipc_binding
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :belongs_to: feat__someip_gateway

Gateway IPC Binding bridges SOCom service discovery and event transport across a local IPC link.
The daemons use the ``mw::com`` (LoLa) implementation. Bridged services use generic proxies and
skeletons for event transport, while ``SomeipdService`` provides peer liveness and transfers the
gateway configuration.

.. uml:: models/component_diagram_gateway_ipc_binding_mw_com.puml
   :align: center
   :caption: Gateway IPC Binding component diagram

Further details
---------------

.. toctree::
   :maxdepth: 1

   mw_com_binding
