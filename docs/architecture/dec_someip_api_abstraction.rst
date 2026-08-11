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

DR-001-Arch: Use socom as SOME/IP stack abstraction
===================================================

**Date:** 2026-08-11

.. dec_rec:: Use socom as SOME/IP stack abstraction
   :id: dec_rec__arch__use_socom_someip_abstraction
   :status: accepted
   :version: 1
   :context: SOME/IP Gateway
   :decision: Option 2 (socom binding)

Context / Problem
-----------------

The SOME/IP Gateway needs an abstraction layer to decouple from the specific SOME/IP stack implementation.
In addition the SOME/IP Gateway is split into two processes (``someipd`` and ``gateway``) for safety reasons.
The semantics of the abstraction layer needs to be transported via IPC to each process.

During the architecture discussion, two main reuse-oriented options were considered:

* Option 1: Implement the SOME/IP stack as a ``mw::com`` binding.
* Option 2: Implement the SOME/IP stack with its own SOME/IP abstraction.

The decision needed to balance implementation complexity, extensibility, and
ability to support gateway-specific runtime behavior.

Options Considered
------------------

Option 1: SOME/IP stack as ``mw::com`` binding
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This option would integrate SOME/IP through the ``mw::com`` binding API and
reuse ``mw::com`` as the central integration layer.

Pros:

* Already safety qualified
* Reuses existing ``mw::com`` integration concepts.
* Keeps one primary communication abstraction in the architecture.

Cons:

* ``mw::com`` is designed to know all bindings up front at compile time
  * To avoid a direct dependency on the SOME/IP stack an interface / abstraction would still be needed at this point
* Would need an additional control channel next to lola for getting event subscription state and FindService
  * Or add SOME/IP specific extensions to lola
* lola is optimized for 1:n communication, but the gateway needs 1:1 communication
* Configuring ``mw::com`` could become complex
  * Assuming that a SOME/IP service is mapped to a ``mw::com`` service
  * ``mw::com`` service configuration format might need SOME/IP specific extensions
  * The ``mw::com`` configuration needs to be present up front at both ``someipd`` and ``gateway`` processes

Option 2: SOME/IP stack with its own SOME/IP abstraction
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This option uses ``socom`` as the abstraction used by the SOME/IP stack.
For implementation of the IPC binding building blocks of `mw::com` are reused.
These are namely:

* ``message_passing``: for message transport between processes
* ``shared_memory``: for shared memory management at each process

Pros:

* ``socom`` provides a simpler API surface for this integration scenario.
* ``socom`` already supports registering bindings, which is needed by the
  selected architecture.
* ``socom`` supports enables a focused SDK-style integration for SOME/IP plugins.
* Keeps API for IPC (``mw::com``) and SOME/IP stack (``socom``) separate, which keeps the scope of each API clear.

Cons:

* Introduces another abstraction alongside ``mw::com`` and requires clear interface boundaries in documentation and code ownership.

Evaluation
----------

Option 1 would require missing capabilities and expected API extensions in
``mw::com`` before it can support the intended SOME/IP gateway behavior cleanly.
To be oblivious of the concrete SOME/IP stack, ``mw::com`` would need to provide a suitable abstraction layer.

Option 2 provides the necessary extensibility (binding registration) with a
simpler integration model and method support out of the box, reducing the amount
of framework adaptation needed for the gateway implementation.

Decision
--------

Option 2 was selected: the SOME/IP stack abstraction in this module is based on
``socom``.

Consequences
------------

* Gateway and daemon integration points are built around ``socom`` contracts.
* ``mw::com`` is not used as the primary abstraction layer for SOME/IP stack
  integration in this module.
* Future work should preserve a clear boundary between ``socom``-based
  abstraction and any optional ``mw::com`` interoperability logic.

References
----------

* SOME/IP Gateway design discussion:
  https://github.com/orgs/eclipse-score/discussions/1984
