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

SOME/IP-SD Section-Level Requirements
=========================================

These are Tier-2, section-level requirements for the ``someip-sd``
chapter of the Open SOME/IP Specification. They are split across three
files so each can be written and reviewed on its own:

- ``message_format``: the ECU-internal interface and the SOME/IP-SD
  message format subtree (SD header, entry format, options format).
- ``messages``: the SOME/IP-SD messages subtree and the communication
  behavior subtree.
- ``pubsub_endpoints``: announcing non-SOME/IP protocols, Publish/
  Subscribe with SOME/IP and SOME/IP-SD, the endpoint handling subtree,
  and the mandatory feature set and basic behavior.

Each requirement in these files refines :need:`feat_req__someip__sd` and
summarizes the normative scope of one specification section. It does not
copy the specification text word for word.

.. toctree::
   :maxdepth: 2

   message_format
   messages
   pubsub_endpoints
