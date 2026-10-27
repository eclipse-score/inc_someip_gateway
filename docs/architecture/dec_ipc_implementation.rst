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

DR-002-Arch: IPC implementation
===============================

**Date:** 2026-08-11

.. dec_rec::   Use socom as SOME/IP stack abstraction
   :id: dec_rec__arch__use_socom_someip_abstraction
   :status: accepted
   :version: 1
   :context: SOME/IP Gateway
   :decision: Option 1 (``mw::com`` / LoLa with GenericSkeleton and GenericProxy)

Context / Problem
-----------------

Due to the ASIL and QM split of the ``gatewayd`` and ``someipd`` processes,
an IPC mechanism is required to transport the semantics of the SOME/IP stack abstraction to each process.

Options Considered
------------------

Option 1: ``mw::com`` / LoLa with GenericSkeleton and GenericProxy as IPC implementation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This option would use ``mw::com`` / LoLa as the IPC implementation for transporting the semantics of the SOME/IP stack abstraction to each process.

Pros:

* Already safety qualified.
* Keeps one primary communication abstraction in the architecture.

Cons:

* LoLa is optimized for 1:n communication, but the gateway needs 1:1 communication.
* Event-driven use cases not so well supported.
* Needs an additional control channel next to LoLa for getting event subscription state and FindService.

  * Or add SOME/IP specific extensions to LoLa.

* Needs config and config can't be just injected via API.

  * The ``mw::com`` configuration needs to be present up front at both ``someipd`` and ``gatewayd`` processes.

* Configuring ``mw::com`` could become complex.

  * Assuming that a SOME/IP service is mapped to a ``mw::com`` service.
  * ``mw::com`` service configuration format might need SOME/IP specific extensions.

* Overall complexity is high.

  * For the SOME/IP Gateway features needed to be added, which are not needed for IPC.
  * The SOME/IP Gateway does not need the full ``mw::com`` feature set.

Option 2: Use pure ``message_passing``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This option uses the ``message_passing`` building block of ``mw::com`` as the IPC implementation for transporting the semantics of the SOME/IP stack abstraction to each process.

Pros:

* One mechanism that can handle everything.

Con:

* Will create problems with safety certification later on. On QNX the caller is blocked if the server is not ready to be called. ⇒ Can't be used in the safe execution phase.
* Overhead for transferring the payload data via message passing.

Option 3: Use ``message_passing`` and ``shared_memory``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For implementation of the IPC binding building blocks of ``mw::com`` are reused.
These are namely:

* ``message_passing``: for message transport between processes
* ``shared_memory``: for shared memory management at each process

Pros:

* Can transfer data within shared memory ⇒ Zero-copy.
* Can use safe mechanisms for signalling in the safe execution phase (e.g. Pulses).

Cons:

* Will add some effort in qualification.

Evaluation
----------

Option 1 would require extending ``mw::com`` and Lola before it can transport the intended SOME/IP gateway semantics cleanly.
The benefit is that it is already safety qualified, but the complexity is the highest of all options.


Option 2 is singled out due to the lack of zero-copy support.

At the time this decision was recorded, Option 3 was preferred because it supports zero-copy while
keeping the implementation complexity manageable.

Decision
--------

The original implementation selected Option 3 and was based on ``message_passing`` and
``shared_memory``. That implementation has since been superseded: ``gatewayd`` and ``someipd`` now
use Option 1, implemented with ``mw::com`` generic proxies and skeletons. The obsolete direct
``message_passing`` and shared-memory binding has been removed.

Consequences
------------

* The integration between the ``gatewayd`` and ``someipd`` daemon is based on ``socom`` interfaces.
* ``mw::com`` is the IPC transport between the daemons.
* ``socom`` remains the service abstraction at the gateway boundary, bridged through the
  ``mw::com`` binding.

References
----------

* SOME/IP Gateway design discussion:
  https://github.com/orgs/eclipse-score/discussions/1984
