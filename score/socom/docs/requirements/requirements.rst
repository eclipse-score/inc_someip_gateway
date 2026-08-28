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

Component SOCom Requirements
############################

Functional Requirements
-----------------------

.. comp_req:: Service-oriented communication runtime
   :id: comp_req__socom__service_runtime
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__socom

   SOCom shall expose ``Runtime``, ``ClientConnector`` and
   ``ServerConnector`` APIs so that services can be offered, requested,
   subscribed to, and their event/method payloads exchanged through a
   pluggable transport backend (gateway IPC binding, mock, or any
   future backend) without exposing the transport details to the
   application.

