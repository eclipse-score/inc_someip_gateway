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

.. comp_req:: SOME/IP Service Discovery handling
   :id: comp_req__someipd__sd_protocol
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__someipd

   ``someipd`` shall implement the SOME/IP Service Discovery (SD)
   protocol entries (``FindService``, ``OfferService``,
   ``StopOfferService``, ``Subscribe``, ``StopSubscribe``,
   ``SubscribeAck``, ``SubscribeNack``) needed by the Open SOME/IP-SD
   specification, so peers can discover and subscribe to SOME/IP
   services offered by this host.

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

.. comp_req:: Local-only network service for gateway-hosted instances
   :id: comp_req__someipd__local_network_service
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__someipd

   The ``LocalNetworkService`` role shall handle SOME/IP messages
   whose target instance is locally hosted by the gateway, so that
   requests originating from remote peers reach the correct local
   application without traversing an additional network hop.

