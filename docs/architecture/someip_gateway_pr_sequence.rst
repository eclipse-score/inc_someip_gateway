..
   *******************************************************************************
   Copyright (c) 2026 Contributors to the Eclipse Foundation

   See the NOTICE file(s) distributed with this work for additional
   information regarding copyright ownership.

   This program and the accompanying materials are made available under the
   terms of the Apache License Version 2.0 which is available at
   https://www.apache.org/licenses/LICENSE-2.0

   SPDX-License-Identifier: Apache-2.0
   *******************************************************************************

SOME/IP Gateway Migration PRs
=============================

The goal is to simplify SOCom in the incubator, then move the retained
communication-protocol behavior and ``gatewayd`` support into
``communication``. SOCom remains an internal protocol layer; ``mw::com`` is
the only application-facing API. The seven Communication PRs below include
their own focused tests.

``someipd`` and its SOME/IP stack are not part of these seven PRs because they
are not ASIL-B compatible. A separate future QM-scoped migration will move
them, outside the ASIL-B ``gatewayd`` boundary.

Incubator Work (Not Counted)
----------------------------

* **#342, merged:** establishes the full-message read-only payload view and
  writable allocation API; it also changes the mw::com bridge sample layout.
* **#322:** separates service identity, versioning, and discovery
  filters, and updates callers. Port the accepted API and ``gatewayd``-relevant
  changes only; exclude ``someipd`` call sites.
* **SOCom simplification refactor, still needed:** after resolving #322's API,
  remove the competing public frontend and features the gateway does not need,
  while preserving and stabilizing the required communication-protocol
  behavior. This is an incubator prerequisite, not one of the seven
  Communication PRs.
* **#291, merged / #348:** #291 introduced the mw::com bridge; #348
  wires both daemons to it. Use the ``gatewayd`` wiring from #348 as migration
  input; do not port its ``someipd`` changes in this sequence.
* **#345/#346:** configurable IPC test setup and startup/event diagrams; useful
  support, not code that needs a Communication port PR.

Communication PRs
-----------------

1. **``feat(socom): add payload records``**

   Port #342's full-message read-only view and writable allocation contract as
   internal protocol types, with payload ownership tests; do not create a
   second application-facing API.

2. **``feat(socom): add service identity and discovery``**

   Port #322's service/version identity and discovery-filter semantics as
   internal protocol behavior, plus required ``gatewayd`` call-site changes;
   exclude ``someipd`` call sites.

3. **``feat(socom): add reduced runtime and registry``**

   Port only the private service registration, event, callback, and lifecycle
   behavior retained by the incubator simplification; do not port public
   connectors or unused SOCom features.

4. **``feat(mw/com/gateway): add local bridge API and configuration``**

   Define the daemon-to-daemon LoLa bridge contract and its configuration.
   Keep it distinct from Communication's existing cross-domain gateway
   ``Transport`` API.

5. **``feat(mw/com/gateway): add LoLa bridge backend``**

   Port the mw::com provider/consumer event bridge from #291 and apply #342's
   sample-layout changes; include focused backend tests.

6. **``feat(mw/com/gateway): add gateway configuration and serializer``**

   Port only configuration owned by ``gatewayd`` and the serializer boundary;
   leave SOME/IP stack, wire-format, and ``someipd`` configuration for the
   separate future migration.

7. **``feat(mw/com/gateway): integrate gatewayd``**

   Port ``gatewayd`` service adapters and executable wiring to the reduced
   SOCom behavior and LoLa bridge, with startup and event-path tests. Do not
   add ``someipd`` to this migration.
