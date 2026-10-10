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

Service identity, versions and discovery
========================================

This design implements the three use cases proposed in
`issue #84 <https://github.com/eclipse-score/inc_someip_gateway/issues/84>`_.
It is a proposal for committer review, not an accepted change to native
requirements. The existing ``comp__socom`` component belongs to
``feat__someip_gateway`` and is classified QM in its component declaration.
The proposed native hierarchy and implementation/test trace are defined in
:doc:`/requirements/component/socom/index` and
:doc:`/requirements/feature/socom/index`. Requirements carry ``invalid``
status and ``proposal`` tags until reviewed, as the pinned metamodel has no
``draft`` requirement status. The separate module-local stakeholder proposal
is in :doc:`/requirements/stakeholder`.

Identity and compatibility
--------------------------

``Service_interface_identifier`` is the canonical identity: string service ID
plus exact major version. Its equality, ordering and hash use those two fields.
``Service_instance_identifier`` adds the offered minor version and instance ID.
Its equality, ordering and hash include all four identity fields.

``Service_interface`` describes a versioned connector contract. For servers its
minor is the offered version; for clients it is the minimum required version.
``get_identifier()`` projects that contract onto its canonical service identity.
Full contracts still compare both versions so bridge requests with different
minimum minors are not incorrectly combined.

The runtime database uses canonical service identities without custom
minor-ignoring comparators. Server registration uses a separate internal
``Service_registration_key``: service identity plus instance ID. Two servers
for the same service and instance collide even when their minor versions differ.
Registration is held while a server is disabled or enabled, and released on
destruction. A later replacement may offer a different minor version.

Client/server compatibility still requires an exact major match and
``client minor <= server minor``. Minor does not change service identity.
The existing fixed-size IPC representation still carries both versions;
its serialization fields and layout are unchanged. Configuration/wire sentinel
translation and the existing narrowing to native 16-bit versions are unchanged;
typed optional discovery filters do not interpret wire sentinel values.

Find-service requests
---------------------

``Find_service_request`` contains an exact service identity, an optional minimum
minor and an optional instance ID. Missing minor accepts every offered minor;
missing instance accepts every instance. A supplied empty instance ID matches
only that ID. Major 255 and minor 65535 are ordinary values, not wildcard
sentinels. No wildcard-major discovery is provided, as requested by issue #84.

``Runtime::find_service`` returns a snapshot of enabled server connectors in
that runtime. Disabled, client-only and destroyed records are excluded. Each
result reports the actual server minor, never a version left in an index key.
Bridge/network discovery remains with registered bridges; this method does not
claim to enumerate offers in other processes or on the network.

The caller supplies an array of optional instance identifiers and its capacity.
The returned count includes every match; only the first ``capacity`` results
are written, in unspecified order. Remaining supplied slots are reset. Passing
``nullptr`` and zero capacity counts matches without writing results. A null
pointer with positive capacity violates the precondition. The caller must
provide valid writable storage and must not access it concurrently.

The runtime holds its existing database mutex while enumerating and copying
records. This method performs no heap allocation and invokes no callbacks,
respecting the SOCom runtime allocation constraint. Register string IDs during
setup, as for existing connector configurations; reuse registered IDs for
queries after startup. The result view uses the existing SCORE span type for
bounded storage access. The snapshot can become
stale after the method returns; discovery does not reserve a connector or
promise continued availability.

Source migration
----------------

The former versioned ``Service_interface_identifier`` is renamed to
``Service_interface``. Connector configurations, compatibility tests, gateway
callers, IPC conversion functions and benchmark callers use that descriptor. The IPC
conversion method is renamed from ``to_socom_identifier`` to
``to_socom_interface`` to describe its full-version result.
Use ``Service_interface::Version`` for the full major/minor pair. Code indexing
services should instead use ``Service_interface_identifier`` and
``major_version``, or call ``contract.get_identifier()``.

The former internal ``Service_instance_identifier`` is renamed to
``Service_registration_key``. The new public instance identifier includes
minor version and must not be used to enforce single-server registration.
``Runtime`` implementations and mocks must implement the new discovery method.
This is a source API migration; rebuilding downstream users is required.

Verification and acceptance
---------------------------

The native unit suite covers canonical identity/hash consistency, full instance
identity, minor boundaries, optional filter combinations, exact service/major
matching, count-only and bounded output, disabled/destroyed offers, and
replacement with a different minor. Existing registration and compatibility
regressions continue to run. Native build/test, sanitizer, documentation and
integration evidence are recorded against the submitted revision in the review
packet. Measurements do not replace committer design review, required IP review
or CI acceptance.

Requirement to design mapping
-----------------------------

* :need:`comp_req__socom__canonical_identity`: canonical identity and database key.
* :need:`comp_req__socom__instance_identity`: offered-instance result model.
* :need:`comp_req__socom__connector_compatibility`: full connector contracts and
  existing client/server compatibility rule.
* :need:`comp_req__socom__registration_identity`: internal registration key and
  lifetime, distinct from the public offered-instance identifier.
* :need:`comp_req__socom__discovery_filters`: typed optional filters and ordinary
  numeric version boundaries.
* :need:`comp_req__socom__available_offers`: enabled local offers and actual minors.
* :need:`comp_req__socom__bounded_discovery`: total count, caller storage bounds,
  clearing and output preconditions.
* :need:`comp_req__socom__discovery_synchronization`: mutex scope, allocation and
  callback constraints, setup IDs and snapshot/caller lifetime obligations.

The first five behaviors follow the issue's version model and preserved
compatibility behavior. Synchronous snapshots, enabled-only enumeration,
bounded caller storage and minimum-minor interpretation are the implementation's
explicit design choices; acceptance of these choices remains with committers.
The eight existing ``comp_req__tc8_conformance__*`` requirements concern network
protocol verification and are not claimed as satisfied by these local tests.
