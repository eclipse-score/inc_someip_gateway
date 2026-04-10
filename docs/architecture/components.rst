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

Components
==========

.. comp:: SOME/IP Gateway Daemon
   :id: comp__gatewayd
   :status: valid
   :safety: ASIL_B
   :security: NO

   The SOME/IP gateway daemon (ASIL-B), bridging the IPC network
   (score::mw::com) and the SOME/IP network by managing local and remote
   service instances.

.. comp:: SOME/IP Network Daemon
   :id: comp__someipd
   :status: valid
   :safety: QM
   :security: NO

   The SOME/IP stack daemon (QM), wrapping vsomeip for all network I/O
   and SOME/IP Service Discovery.
