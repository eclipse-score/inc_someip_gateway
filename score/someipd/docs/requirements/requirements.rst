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

Component someipd Requirements
##############################

Functional Requirements
-----------------------

.. comp_req:: Route SOME/IP traffic between remote peers and local clients
   :id: comp_req__someipd__remote_local_routing
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__someipd

   ``someipd`` shall run the SOME/IP protocol stack and route messages
   between remote peers on the network (``RemoteNetworkService``) and
   local IPC clients (``LocalNetworkService``) via its ``Routing``
   subsystem, translating between on-wire SOME/IP frames and the
   internal IPC representation without exposing the network to local
   clients.

