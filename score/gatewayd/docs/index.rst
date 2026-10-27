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

.. _gatewayd:

gatewayd
########

Abstract
========

The gateway daemon. Bridges local IPC service instances with remote SOME/IP service instances across the ASIL boundary, handling service offers, requests, event subscriptions, and payload transfer.


.. comp:: SOME/IP Gateway Daemon
   :id: comp__gatewayd
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway

   Connects the :need:`comp__someipd` to the application via LoLa IPC (mw::com).
