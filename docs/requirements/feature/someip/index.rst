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

SOME/IP Specification Requirements
===================================

This section imports the Open SOME/IP Specification
(`open-someip-spec <https://github.com/some-ip-com/open-someip-spec>`_,
license ``Community-Spec-1.0``) into the S-CORE requirements tree, in two
tiers.

- **Tier 1** (``protocol``): five broad requirements, one per chapter of the
  specification. These map one to one with the specification's five chapter
  files and stay stable over time.
- **Tier 2** (``compat``, ``tp``, and the ``rpc``/``sd`` subdirectories):
  one requirement per specification section that has at least one normative
  statement. Each Tier-2 requirement points back to its parent Tier-1
  chapter requirement with a ``Refines`` reference in its body, and lists
  the specification Requirement IDs it covers.

A ``feat_req`` cannot use ``:derived_from:`` to point to another
``feat_req``. So every requirement below, in both tiers, uses
``:derived_from: stkh_req__someip_gw__interoperability`` directly instead.
The link from chapter to section is shown by document nesting, the
``Refines`` reference in the body, and matching ``:tags:`` values.

.. toctree::
   :maxdepth: 2

   protocol
   compat
   tp
   rpc/index
   sd/index
