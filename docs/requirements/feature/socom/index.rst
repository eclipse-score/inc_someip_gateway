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
   # SPDX-License-Identifier: Apache-2.0 AND CC0-1.0
   # AI Disclosure: Generated with OpenAI Codex (model revision unavailable).
   # AI-generated portions are offered under CC0-1.0; copyrightable curation
   # retains Apache-2.0. Human review is pending.
   # *******************************************************************************

SOCom version selection
=======================

These requirements are **proposals derived from issue #84**, pending committer
review. The pinned docs-as-code 8.2.0 metamodel permits only ``valid`` or
``invalid`` requirement status. Consequently, ``invalid`` with the
``proposal`` tag means not yet accepted here; it does not claim that the
implemented behavior has failed verification. QM and security NO follow the
existing SOCom component declaration and remain subject to classification review.
No approved release applicability or stakeholder acceptance is asserted.

.. feat_req:: SOCom version identity and local discovery
   :id: feat_req__socom__version_selection
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :valid_from: v0.1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :derived_from: stkh_req__someip_gw__local_version_selection
   :satisfied_by: feat__someip_gateway

   The gateway middleware abstraction shall separate canonical service identity
   from full offered-instance identity and full connector contracts, and expose
   bounded local discovery with optional minimum-minor and instance filters.

   Interpreting a supplied minor as a minimum compatible version preserves the
   existing client/server rule; this interpretation and the new public discovery
   API are design proposals requiring committer acceptance.
