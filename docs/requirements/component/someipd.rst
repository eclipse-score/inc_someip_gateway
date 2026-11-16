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

someipd Component Requirements
==============================

These requirements cover externally observable behavior of the SOME/IP
network daemon.

.. comp_req:: someipd prints usage on request
   :id: comp_req__someipd__prints_usage
   :status: valid
   :version: 1
   :safety: QM
   :security: NO
   :reqtype: Interface
   :satisfied_by: comp__someipd

   When started with the ``--help`` command-line argument, someipd shall
   print its usage text and exit with a success exit code.

.. comp_req:: someipd exits successfully on termination signals
   :id: comp_req__someipd__exits_on_signal
   :status: valid
   :version: 1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :satisfied_by: comp__someipd

   When someipd receives SIGTERM or SIGINT while running, it shall stop and
   exit with a success exit code.
