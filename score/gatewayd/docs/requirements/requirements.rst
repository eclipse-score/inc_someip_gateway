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

