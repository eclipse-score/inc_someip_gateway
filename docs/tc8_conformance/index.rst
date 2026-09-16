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

- **Protocol Conformance**: Tests ``someipd`` at the wire level using raw
  UDP/TCP sockets and `scapy <https://scapy.net/>`_ as the packet serializer
  and parser. No application processes are needed. ``someipd`` is launched on
  its own (``-c <config.bin>``, without ``gatewayd``), so no local/remote
  application traffic is routed through it.

- **Application-Level Tests**: Tests the full gateway path
  (mw::com client to ``gatewayd`` to ``someipd`` to network) using C++ apps
  built on ``score::mw::com``. These tests are stack-agnostic.

All tests live under ``tests/tc8_conformance/`` and share the ``tc8`` /
``conformance`` Bazel tags. For the architectural overview, test topology
diagrams, and module structure, see
:doc:`/architecture/tc8_conformance_testing`.

.. note::

   Protocol conformance tests run ``someipd`` on its own, without
   ``gatewayd``. Fixture defaults (DUT IP, tester IP, port numbers) are defined in
   ``tc8_itf_conftest.py``.  To run all protocol conformance tests::

      bazel test --config=tc8-itf //tests/tc8_conformance/...

   For QNX x86_64 use ``--config=tc8-itf-qnx``.

.. toctree::
   :maxdepth: 2

   requirements.rst

.. seealso::

   :doc:`/architecture/tc8_conformance_testing` for test topology, module
   dependency diagrams, and planned components.
