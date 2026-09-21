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

Requirements
============

This section contains all requirements for the SOME/IP Gateway, organized
following the S-CORE docs-as-code requirements process.

.. toctree::
   :maxdepth: 2

   stakeholder
   feature/index
   component/index

Organization
------------

Requirements are structured in a three-level hierarchy following the
S-CORE requirements engineering process:

.. list-table:: Requirements Hierarchy
   :widths: 20 30 50
   :header-rows: 1

   * - Level
     - Directory
     - Purpose
   * - Stakeholder
     - ``requirements/stakeholder.rst``
     - High-level needs and constraints from users, integrators, and safety
   * - Feature
     - ``requirements/feature/``
     - Derived from stakeholder requirements, scoped to a gateway feature
   * - Component
     - ``requirements/component/``
     - Derived from feature requirements, scoped to a specific component

Component Names
^^^^^^^^^^^^^^^

The following component identifiers are used in requirement IDs and file names:

.. list-table:: Component Identifiers
   :widths: 25 15 60
   :header-rows: 1

   * - Component
     - Safety
     - Description
   * - ``gatewayd``
     - ASIL-B
     - Gateway daemon — bridges IPC and SOME/IP, E2E protection, ACL enforcement
   * - ``someipd``
     - QM
     - SOME/IP stack daemon — handles network I/O and SOME/IP-SD
   * - ``network_service``
     - ASIL-B
     - IPC interface between ``gatewayd`` and ``someipd`` (SomeipMessageTransfer)

Requirement Identifier Scheme
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

IDs follow the pattern ``<type>__<scope>__<title_snake_case>``:

.. list-table:: Identifier Format
   :widths: 20 40 40
   :header-rows: 1

   * - Type Prefix
     - Format
     - Example
   * - ``stkh_req``
     - ``stkh_req__some_ip_gateway__<title>``
     - ``stkh_req__some_ip_gateway__transparent_bridging``
   * - ``feat_req``
     - ``feat_req__<scope>__<title>``
     - ``feat_req__someip__rpc`` (in ``requirements/feature/someip/protocol.rst``)
   * - ``comp_req``
     - ``comp_req__<component>__<title>``
     - ``comp_req__someipd__rpc_header`` (in ``requirements/component/someipd/protocol.rst``)
   * - ``aou_req``
     - ``aou_req__<component>__<title>``
     - ``aou_req__gatewayd__valid_config``

Document Heading Standards
^^^^^^^^^^^^^^^^^^^^^^^^^^

All RST requirement files follow this structure:

1. Copyright header (RST comment block)
2. Document title — ``=`` overline/underline
3. Descriptive introduction paragraph
4. Requirement directives grouped by topic
5. Sections within a file use ``-`` underline, subsections use ``^``

Mandatory Attributes
^^^^^^^^^^^^^^^^^^^^

Every requirement directive must include these attributes:

- ``:id:`` — unique identifier per the scheme above
- ``:status:``, either ``valid`` or ``invalid`` (``draft`` is **not** an
  accepted value for requirement nodes per the metamodel)
- ``:safety:`` — ``QM`` or ``ASIL_B``
- ``:security:`` — ``YES`` or ``NO``
- ``:reqtype:`` — ``Functional``, ``Interface``, ``Process``, or ``Non-Functional``
- ``:satisfied_by:``, the corresponding architecture element ID
  (``feat`` for ``feat_req``, ``comp`` for ``comp_req``); this is the
  **mandatory** parent/downstream link, not ``:satisfies:``
- ``:derived_from:``, parent requirement ID (``stkh_req`` for ``feat_req``,
  ``feat_req`` for ``comp_req``); this is an *optional* upward
  traceability link, not mandatory
- ``:rationale:`` — justification text (mandatory for stakeholder level only)

.. note::

   The metamodel's ``id_contains_feature`` validator (enforced as a build
   error) requires that for every ``feat_req``/``comp_req`` ID, the middle
   ``<scope>`` segment appear as a substring of the directory path of the
   RST file the requirement lives in. This is why new ``feat_req``/
   ``comp_req`` nodes for a given scope (e.g. ``someip``, ``someipd``) are
   placed in their own subdirectory (e.g.
   ``requirements/feature/someip/protocol.rst``,
   ``requirements/component/someipd/protocol.rst``) rather than directly
   under the flat ``requirements/feature/`` or ``requirements/component/``
   directory.

Cross-References
^^^^^^^^^^^^^^^^

- TC8 conformance test requirements are maintained in :doc:`/tc8_conformance/index`
  alongside their test specifications.
