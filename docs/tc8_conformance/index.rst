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
  and parser. ``someipd``, the ETS stub, and ``gatewayd`` are all started
  together as part of the DUT stack, but no application processes drive
  traffic through ``gatewayd`` in these tests.

- **Application-Level Tests**: Tests the full gateway path
  (mw::com client to ``gatewayd`` to ``someipd`` to network) using C++ apps
  built on ``score::mw::com``. These tests are stack-agnostic.

All tests live under ``tests/tc8_conformance/`` and share the ``tc8`` /
``conformance`` Bazel tags. For the architectural overview, test topology
diagrams, and module structure, see
:doc:`/architecture/tc8_conformance_testing`.

.. note::

   Protocol conformance tests run the full DUT stack (``someipd``, the ETS
   stub, and ``gatewayd``), though only ``someipd`` is exercised directly.
   Fixture defaults (DUT IP, tester IP, port numbers) are defined in
   ``tc8_itf_conftest.py``.  To run all protocol conformance tests::

      # Normal tests (parallel)
      bazel test --config=tc8-itf //tests/tc8_conformance/...
      # Timing-sensitive tests (serial, explicit manual suite)
      bazel test --config=tc8-itf-timing //tests/tc8_conformance:timing_sensitive_tests

   Run these commands sequentially. Both sets are required: wildcard runs skip
   the manual timing-sensitive tests and their suite. Normal configs use
   ``--test_tag_filters=tc8,-timing_sensitive``, excluding timing-sensitive tests
   even when selected explicitly. Timing configs override that inherited filter
   with ``--test_tag_filters=timing_sensitive`` and use ``--jobs=1`` instead of
   ``exclusive`` tags. Positive tag filters are ORed, so the timing filter must
   not also include ``tc8``. Use the timing config for individual timing-sensitive
   targets as well.

   For QNX x86_64 run the corresponding pair sequentially::

      bazel test --config=tc8-itf-qnx //tests/tc8_conformance/...
      bazel test --config=tc8-itf-qnx-timing //tests/tc8_conformance:timing_sensitive_tests

.. toctree::
   :maxdepth: 2

   requirements.rst
   test_specification.rst
   traceability.rst

.. seealso::

   :doc:`/architecture/tc8_conformance_testing` for test topology, module
   dependency diagrams, and planned components.

   :doc:`traceability`: OA TC8 v3.0 Chapter 6 test case mapping, requirement
   links, and generated test results.
