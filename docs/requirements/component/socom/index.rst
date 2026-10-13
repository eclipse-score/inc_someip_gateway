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
   # AI Disclosure: Assisted by OpenAI Codex (GPT-6.1 Sol).
   # *******************************************************************************

SOCom version handling requirements
===================================

These requirements are **proposals derived from issue #84**, pending committer
review. The pinned docs-as-code 8.2.0 metamodel permits only ``valid`` or
``invalid`` requirement status. Consequently, ``invalid`` with the
``proposal`` tag means not yet accepted here; it does not claim that the
implemented behavior has failed verification. QM and security NO follow the
existing SOCom component declaration and remain subject to classification review.
No approved release applicability or stakeholder acceptance is asserted.

Source: `issue #84 <https://github.com/eclipse-score/inc_someip_gateway/issues/84>`_.
Design and verification rationale: :doc:`/socom/version_handling`.

.. comp_req:: Canonical service identity
   :id: comp_req__socom__canonical_identity
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   Service identity equality, ordering and hashing shall depend only on the exact registered service ID and unsigned 16-bit major version; differing minor versions shall share canonical identity.

.. comp_req:: Full offered instance identity
   :id: comp_req__socom__instance_identity
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   An offered instance identifier shall contain service ID, major version, actual offered unsigned 16-bit minor version and instance ID. Equality, ordering and hashing shall include all four fields.

.. comp_req:: Versioned connector compatibility
   :id: comp_req__socom__connector_compatibility
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   Full connector contracts shall retain both major and minor versions. A client shall be compatible only with an offer having the same service ID and major version and an offered minor at least its required minor; full contract equality shall distinguish different minors.

.. comp_req:: Single server registration identity
   :id: comp_req__socom__registration_identity
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   The runtime shall reject simultaneous server registrations sharing service ID, major version and instance ID irrespective of minor version. Disabled servers shall retain registration; destruction shall release it and permit a replacement with a different minor.

.. comp_req:: Optional local discovery filters
   :id: comp_req__socom__discovery_filters
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   A find request shall match the exact service ID and major version. An absent minor or instance filter shall accept any value of that field; a supplied minor shall be a minimum and a supplied instance shall match exactly, including an empty ID. Major 255 and minor 65535 shall be ordinary values, not wildcard sentinels.

.. comp_req:: Available local offers and actual versions
   :id: comp_req__socom__available_offers
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   Local discovery shall enumerate only enabled server connectors in the queried runtime, report each actual server minor, and exclude disabled, destroyed and client-only records. Replacing an offer shall report the replacement minor rather than a retained index-key minor.

.. comp_req:: Bounded discovery output
   :id: comp_req__socom__bounded_discovery
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   Local discovery shall return the total matching count and write at most the supplied capacity, in unspecified order, clearing unused supplied slots. A null output pointer with zero capacity shall count without writing; a null pointer with positive capacity shall violate the precondition.

.. comp_req:: Synchronized local snapshot
   :id: comp_req__socom__discovery_synchronization
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: feat_req__socom__version_selection
   :satisfied_by: comp__socom

   Local discovery shall hold the runtime database mutex while enumerating and copying the snapshot, perform no heap allocation and invoke no callbacks. The snapshot shall not reserve a connector or guarantee availability after return. Query IDs shall be registered during setup and caller output shall be valid and exclusively accessible.

Verification limits
-------------------

Test links below use ``PartiallyVerifies`` because individual cases exercise
parts of each requirement. Link presence is not proof of complete verification.
Allocation and callback absence, locking scope, caller obligations and snapshot
lifetime require implementation/design review in addition to the concurrency
test. IPC layout preservation and downstream source migration are design
constraints verified separately; these requirements do not claim wire-level
or external-runtime interoperability acceptance.

.. needtable::
   :filter: type == 'comp_req' and 'issue_84' in tags
   :columns: id;status;derived_from;satisfied_by;source_code_link;testlink
