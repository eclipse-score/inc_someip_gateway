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

TC8 SOME/IP Conformance Testing
================================

This section defines the requirements, test specifications, and traceability
for `OPEN Alliance TC8 <https://opensig.org/tech-committee/tc8-automotive-ethernet-ecu-test-specification/>`_
SOME/IP conformance testing of the SOME/IP Gateway.

The TC8 test suite covers two scopes:

- **Protocol Conformance** — Tests ``someipd`` at the wire level using raw
  UDP/TCP sockets and the ``someip`` Python package. No application processes
  are needed. ``someipd`` runs in ``--tc8-standalone`` mode.

- **Application-Level Tests** — Tests the full gateway path
  (mw::com client → ``gatewayd`` → ``someipd`` → network) using C++ apps
  built on ``score::mw::com``. These tests are stack-agnostic.

All tests live under ``tests/tc8_conformance/`` and share the ``tc8`` /
``conformance`` Bazel tags. For the architectural overview, test topology
diagrams, and module structure, see
:doc:`/architecture/tc8_conformance_testing`.

.. toctree::
   :maxdepth: 2

   requirements.rst
   test_specification.rst
   traceability.rst

.. seealso::

   :doc:`/architecture/tc8_conformance_testing` — full OA TC8 v3.0 Chapter 5
   scope analysis, gap analysis, and coverage breakdown (19% of 230 spec test
   cases).
