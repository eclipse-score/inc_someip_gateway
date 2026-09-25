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

SOME/IP RPC Section-Level Requirements
=========================================

These are Tier-2, section-level requirements for the ``someip-rpc``
chapter of the Open SOME/IP Specification. They are split across three
files so each can be written and reviewed on its own:

- ``wire_format``: identifier definitions, transport protocol,
  endianness, and the message header (and its children).
- ``serialization``: serialization of parameters and data structures (and
  its children).
- ``rpc_patterns``: transport protocol bindings, request/response,
  fire-and-forget, events, fields, and error handling.

Each requirement in these files refines :need:`feat_req__someip__rpc` and
summarizes the normative scope of one specification section. It does not
copy the specification text word for word.

.. toctree::
   :maxdepth: 2

   wire_format
   serialization
   rpc_patterns
