..
   # *******************************************************************************
   # Copyright (c) 2025 ETAS GmbH
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

Architectural Design Decision: Dynamic Registration for SOME/IP Gateway
=======================================================================

.. dec_rec:: Dynamic Registration for SOME/IP Gateway
   :id: dec_rec__component__someip_gw_registration
   :status: proposed
   :decision:
      Gateway processes will dynamically register their communication requirements (e.g., required services and events) with the central SOME/IP Communication Stack process. This will be achieved via a dedicated, message-based IPC interface.
   :context:
      The SOME/IP Gateway architecture separates the core communication stack from multiple gateway processes. This is necessary for safety separation (ASIL vs. QM) and to manage a single network socket, which can only be bound by one process. A mechanism is required for the gateway "client" processes to inform the central "server" stack of their routing needs at runtime.


Alternatives Considered
-----------------------

Static Configuration File
^^^^^^^^^^^^^^^^^^^^^^^^^
A central, static configuration file (e.g., in JSON or XML format) would define the complete routing table for all possible gateway processes.
The SOME/IP Communication Stack would parse this file at its startup and configure all possible routes.
Gateway processes would also read this file to understand their respective roles.

Advantages
""""""""""
*  **Simplicity:**
   No complex runtime IPC protocol for registration is needed.
   The system's entire configuration is declarative and visible in one place.
*  **Predictability:**
   System behavior is fixed at startup, which can simplify system analysis and testing.
*  **Low Runtime Overhead:**
   No resources are spent on registration messages during operation.

Disadvantages
"""""""""""""
*  **Inflexible and Static:**
   Any change, such as adding a new gateway process or modifying an existing one's routes, requires a restart of the central SOME/IP stack.
   This would cause a temporary loss of all external communication for the entire system, not just the affected gateway.
*  **Lack of Resilience:**
   The SOME/IP stack has no knowledge of whether a gateway process is actually running.
   It may attempt to forward data to a crashed or non-existent process, leading to data loss and wasted resources.
*  **Tight Coupling:**
   The central stack's configuration is coupled to the existence of every potential gateway client.


Shared Memory Control Block
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The SOME/IP stack would create a shared memory segment with a well-defined data structure (a "control block").
This structure would contain slots for gateway processes to write their registration information (e.g., PID, required service IDs).
The SOME/IP stack would periodically poll this memory to discover and update its routing table.

Advantages
""""""""""
*  **High Performance:**
   Bypasses the kernel for data exchange, offering very low latency communication between the processes.

Disadvantages
"""""""""""""
*  **High Complexity & Risk:**
   Requires complex and error-prone synchronization mechanisms (e.g., mutexes, semaphores within the shared memory) to prevent race conditions.
   A gateway crashing while holding a lock could deadlock the entire system.
   Unfortunately this kind of communication pattern is not implemented in LoLa (unlike iceoryx2 with the blackboard pattern) which would abstract all those complexities away.
*  **Brittle:**
   The data structures are tightly coupled.
   A change in the control block structure requires recompiling all participating processes.
   This would be acceptable though in this case because the gateway and the SOME/IP stack are developed together.
*  **Implicit Liveness:**
   Detecting a crashed gateway is not straightforward.
   It would require implementing a heartbeat mechanism within the shared memory, adding further complexity.


One SOME/IP Stack per Gateway Process
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each gateway process would embed its own instance of the SOME/IP communication stack, linking it as a library. Each process would then attempt to manage its own network socket.

Advantages
""""""""""
*  **Maximum Decoupling:**
   Processes are completely self-contained with no runtime dependency on a central stack for network access.

Disadvantages
"""""""""""""
*  **Fundamentally Infeasible:**
   This approach is not viable under the core constraint.
   Multiple processes **cannot** bind to the same network device and port simultaneously.
   This would lead to "Address already in use" errors for all but the first process to start.
   While they could use different ports, this would violate the SOME/IP service architecture, where a service is expected at a specific, well-known port.


Justification for the Decision
------------------------------

The **Dynamic Registration via IPC** approach was chosen because it provides the best balance of flexibility, robustness, and manageable complexity.

*  **Flexibility and Scalability:**
   This is the primary advantage.
   New gateway processes can be introduced, and existing ones can be updated and restarted independently without affecting the central SOME/IP stack or any other gateway.
   This is essential for a dynamic, resilient system.
*  **Robust State Management:**
   The registration protocol provides explicit control over the system's state.
   The central stack knows exactly which gateways are active and what their requirements are.
   The IPC connection's liveness provides a reliable and immediate mechanism to detect client failures and clean up associated routes, preventing data loss to dead processes.
*  **Clear Separation of Concerns:**
   It enforces the client-server model cleanly.
   The IPC protocol becomes a well-defined API contract between the components, allowing them to be developed, tested, and maintained independently.

While this approach introduces the overhead of defining and managing an IPC protocol, this complexity is justified by the significant gains in flexibility and resilience when compared to the static alternatives.
It is far less complex and risky than implementing a custom shared memory solution, and it is the only viable option that respects the fundamental networking constraints of the operating system.
