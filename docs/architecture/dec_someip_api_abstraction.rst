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

DR-001-Arch: SOME/IP stack abstraction
===================================================

**Date:** 2026-08-11

.. dec_rec:: SOME/IP stack abstraction
   :id: dec_rec__arch__someip_abstraction
   :status: accepted
   :version: 1
   :context: SOME/IP Gateway
   :decision: Option 2 (socom binding)

Context / Problem
-----------------

``vsomeipd`` will not be the only SOME/IP stack implementation used in the future, and the architecture needs to support multiple implementations.
The SOME/IP Gateway needs an abstraction layer to decouple from the specific SOME/IP stack implementation.
The semantics of the abstraction layer need to be reflected via the IPC layer.

The abstraction layer is the scope of this decision record. The IPC implementation is covered in a separate decision record.

Options Considered
------------------

Option 1: ``mw::com`` as abstraction API (binding)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This option would integrate SOME/IP network stack through the ``mw::com`` binding API and
reuse ``mw::com`` as the central integration layer.

Pros:

* Already safety qualified
* Reuses existing ``mw::com`` integration concepts.
* Keeps one primary communication abstraction in the architecture.

Cons:

* ``mw::com`` is designed to know all bindings up front at compile time

  * To avoid a direct dependency on the SOME/IP stack an interface / abstraction would still be needed at this point

* ``mw::com`` might need API extension for event subscription state and FindService

* Configuring ``mw::com`` could become complex

  * Assuming that a SOME/IP service is mapped to a ``mw::com`` service
  * ``mw::com`` service configuration format might need SOME/IP specific extensions

Option 2: Abstract API as SOME/IP abstraction
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This option uses ``socom`` as the abstraction to interact with the SOME/IP network stack.
The API of ``socom`` is designed to be a generic abstraction of the SOME/IP network stack without serialization support.
``socom`` is designed to be as simple as possible, while still providing the necessary functionality to implement a SOME/IP stack abstraction.
A SOME/IP network stack implementation can be registered at startup.

Pros:

* ``socom`` provides a simpler API surface for this integration scenario.
* ``socom`` already supports registering bindings, which is needed by the
  selected architecture.
* ``socom`` supports enables a focused SDK-style integration for SOME/IP plugins.
* Keeps API for IPC (``mw::com``) and SOME/IP stack (``socom``) separate, which keeps the scope of each API clear.
* Supports zero-copy.
* API loosely coupled with the exact stack.
* Abstraction of the service handling. It shields ``gatewayd`` from all stack interaction logic ("Glue code").
* Separation of "bridge" and "API".

Cons:

* Introduces another abstraction alongside ``mw::com`` and requires clear interface boundaries in documentation and code ownership.
* More complex compare to option 3.

  * This was dealt with by removing all features currently not needed by the gateway.

Option 3: Abstraction close to ``vsomeip`` API as SOME/IP abstraction
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since we start with ``vsomeip`` as the initial SOME/IP stack implementation, we could also consider an abstraction layer that is close to the ``vsomeip`` API.

Pros:

* API design already mostly determined by vsomeip API.

Cons:

* API not designed for zero-copy.
* API closely coupled with the exact stack.
* No abstraction of the service handling, all stack interaction logic ("Glue code") propagates into the gatewayd.
* Separation of "bridge" and "API" would still have to be designed.

Evaluation
----------

Option 1 would require missing capabilities and expected API extensions in
``mw::com`` before it can support the intended SOME/IP gateway behavior cleanly.
To be agnostic to the concrete SOME/IP stack, ``mw::com`` would need to provide a suitable abstraction layer.

Thus if option 1 is selected, we still have to select between option 2 and 3 for the abstraction layer.

Option 2 provides the necessary extensibility (binding registration) with a
simpler integration model and method support out of the box.

Decision
--------

Option 2 was selected: the SOME/IP stack abstraction in this module is based on
``socom``.

Consequences
------------

* The integration between the ``gatewayd`` and ``someipd`` daemon is based on ``socom`` interfaces.
* ``mw::com`` is not used as the primary abstraction layer for SOME/IP stack
  integration in this module.
* Future work should preserve a clear boundary between ``socom``-based
  abstraction and any optional ``mw::com`` interoperability logic.

References
----------

* SOME/IP Gateway design discussion:
  https://github.com/orgs/eclipse-score/discussions/1984
