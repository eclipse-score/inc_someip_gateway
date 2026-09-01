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

Quality Pack Targets
####################

The ``score_someip_gateway`` module plugs into the Score docs-as-code
dashboards and quality gates as described in the upstream how-to:
https://eclipse-score.github.io/docs-as-code/main/how-to/dashboards_and_quality_gates.html.

The Bazel targets below are the ones consumed by CI to produce
dashboard artefacts and to enforce traceability thresholds.

Unit tests
==========

- **Tag:** ``unit`` (carried by every ``cc_test`` intended as a unit
  test, currently ``//score/serializer:null_serializer_test`` and
  ``//score/socom/test/unit:socom_test``).
- **Aggregate target:** ``//:unit_tests``.
- **Command:** ``bazel test //:unit_tests``.
- **Results:** JUnit XML and stdout log per test target under
  ``bazel-testlogs/<package>/<test>/{test.log,test.xml}``.

Component tests
===============

Component tests exercise a component together with real collaborators
(SOCom runtime plus the gateway IPC binding, or the SOCom stress harness).
They cross more than one translation unit and rely on the real
``message_passing`` / shared-memory transports.

- **Tag:** ``component``.
- **Aggregate target:** ``//:component_tests``.
- **Command:** ``bazel test //:component_tests``.
- **Included tests (existing tests reclassified, not new ones):**

  - ``//score/gateway_ipc_binding/test:gateway_ipc_binding_test``
  - ``//score/socom/test/stress:socom_stress_test``

- **Results:** JUnit XML and stdout log per test target under
  ``bazel-testlogs/<package>/<test>/{test.log,test.xml}``.

Code coverage
=============

- **Config:** ``coverage.bazelrc`` (LLVM ``llvm-cov`` toolchain).
- **Command:** ``bazel coverage --config=coverage //:unit_tests //:component_tests``.
- **Results:** raw ``lcov`` data under
  ``$(bazel info output_path)/_coverage/_coverage_report.dat``. Report
  post-processing (HTML / Cobertura) uses the standard
  ``score_tooling`` coverage flow shared with the rest of S-CORE.

Requirements traceability (dashboards + gate)
=============================================

Component requirements live alongside each component under
``score/<component>/docs/requirements/requirements.rst`` and use the
Score metamodel ``comp_req::`` directive. The externally-visible
feature-level requirements (``feat_req__some_ip_gateway__*``) belong to
the upstream ``eclipse-score/score`` repo (see
`Open SOME/IP <https://github.com/some-ip-com/open-someip-spec>`_ for
the protocol these features track). Once ``@score_platform`` /
``@score_process`` needs.json can be consumed cleanly from this repo,
they will be pulled in via the ``external_needs`` attribute of the
root ``docs()`` macro.

Source-code and test-code links are consumed by ``score_docs_as_code``.
See the upstream how-to for the marker syntax, the required GoogleTest
``RecordProperty`` fields, and the validation rules that decide whether a
link is counted:
https://eclipse-score.github.io/docs-as-code/main/how-to/dashboards_and_quality_gates.html.

Repo-local wiring:

- **Source-code markers** live in the C++ implementation files. Per
  component, the marked files are collected into a
  ``requirement_marked_sources`` ``filegroup`` (for example
  ``//score/gatewayd:requirement_marked_sources``) and passed to that
  component's ``docs_bundle`` via ``code_targets``. Grep the tree for
  ``# req-Id:`` to see current examples.
- **Test-code links** use ``RecordProperty`` inside the test body. Grep
  for ``FullyVerifies`` to see current examples (for instance
  ``//score/socom/test/unit:socom_test``).
- **Bazel target:** ``//:docs`` — HTML output plus ``_build/needs.json``
  and ``_build/metrics.json`` (traceability metrics extracted from needs).
- **Local flow** — order matters, the docs build reads
  ``bazel-testlogs`` for test links:

  .. code-block:: bash

     bazel test //:unit_tests //:component_tests
     bazel run  //:docs

Live coverage numbers are posted as a sticky comment on every pull
request by ``.github/workflows/quality_pack_comment.yml`` (rendered by
``tools/quality_pack/render_pr_comment.py`` from ``_build/metrics.json``);
see the workflow's uploaded ``quality-pack-metrics`` artifact for the
raw JSON. The initial gate thresholds — matching the baseline at
introduction so the gate never regresses without a follow-up ticket —
are:

  ====================  =====  ===============================
  Threshold             Value  Meaning
  ====================  =====  ===============================
  min-req-code             67  ``with_code_link_pct`` ≥ 67
  min-req-test              9  ``with_test_link_pct`` ≥ 9
  min-req-fully-linked      9  ``fully_linked_pct`` ≥ 9
  min-tests-linked          1  ≥ 1 test carrying ``FullyVerifies``
  ====================  =====  ===============================
