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

Component gatewayd Requirements
###############################

Functional Requirements
-----------------------

.. comp_req:: Bridge local IPC and remote SOME/IP service instances
   :id: comp_req__gatewayd__local_remote_bridging
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__gatewayd

   ``gatewayd`` shall present local IPC service instances
   (``LocalServiceInstance``) and remote SOME/IP service instances
   (``RemoteServiceInstance``) to SOCom and shall forward service
   offers, requests, event subscriptions and event updates between the
   two sides so that an application client is unaware of the transport
   layer.

.. comp_req:: Publish locally hosted service instances on the SOME/IP network
   :id: comp_req__gatewayd__publish_local_instance
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__gatewayd

   For every ``local_service_instance`` in the configuration
   ``gatewayd`` shall instantiate a ``LocalServiceInstance`` that
   translates local IPC service offers and event updates into SOME/IP
   Service Discovery *Offer Service* entries and SOME/IP messages on
   the network side.

.. comp_req:: Subscribe to remote SOME/IP service instances on behalf of local clients
   :id: comp_req__gatewayd__consume_remote_instance
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__gatewayd

   For every ``remote_service_instance`` in the configuration
   ``gatewayd`` shall instantiate a ``RemoteServiceInstance`` that
   consumes SOME/IP Service Discovery *Find/Offer* exchanges and
   forwards received SOME/IP messages to any local IPC clients that
   subscribed to that instance via SOCom.

