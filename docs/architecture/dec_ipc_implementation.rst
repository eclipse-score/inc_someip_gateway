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
Gateway specific RPC calls are done via a dedicated `mw::com` service.

Pros:

* Already safety qualified.
* Keeps one primary communication abstraction in the architecture.

Cons:

* Event-driven use cases not so well supported.

* Configuring ``mw::com`` could become complex.

  * Assuming that a SOME/IP service is mapped to a ``mw::com`` service.
  * ``mw::com`` service configuration format might need SOME/IP specific extensions.

* Overall complexity is high.

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

Option 1 would build on top of ``mw::com`` with code reuse and inheriting its safety qualification.

Option 2 is singled out due to the lack of zero-copy support.

Option 3 leads to writing lots of IPC handling code for both message passing and shared memory.
It then must also be safety qualified, which adds additional effort and complexity.

Decision
--------

Option 1 was selected: the IPC implementation is based on ``mw::com``.

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
