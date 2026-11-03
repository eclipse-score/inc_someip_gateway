..
   # *******************************************************************************
   # Copyright (c) 2025 Contributors to the Eclipse Foundation
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

Behavioral View
===============

SOCom coordinates runtime service matching, connector communication, cross-domain service bridging, and callback deadlock detection. The connector lifecycle is governed by two state machines:

- :ref:`event subscription state <event_subscription_state_machine>`
- :ref:`service state <service_state_machine>`

Runtime
-------

``Runtime`` acts as the process-internal service instance broker and connector factory:

- **Lifecycle tracking**: Creates client and server connectors and tracks their existence and enabled/disabled state.
- **Automatic matching**: Connects and disconnects compatible ``Client_connector`` and ``Server_connector`` instances based on service interface, version, and instance ID.
- **Bridge forwarding**: Provides a service bridge registration interface to forward service discovery and requests across process or network boundaries.

Server connector
----------------

A service provider owns a server connector to participate in SOCom communication and interact with connected ``Client_connector`` instances. The server connector API provides the following callbacks and operations:

- Remote procedure call invocation callback.
- Event subscription state change callback:

  - on first subscriber (``Event_state::subscribed``)
  - on last unsubscriber (``Event_state::unsubscribed``)

- Event update request callback (for initial field values or on-demand requests).
- Event mode query (event with or without initial update).
- Event payload allocation (``allocate_event_payload()``) for zero-copy transmission.
- Event publication (``update_event()`` and ``update_requested_event()``).

Dynamic Architecture
--------------------

Event and Method Communication
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. comp_arc_dyn:: SOCom Event and Method Communication
   :id: comp_arc_dyn__socom_design__event_flow
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__socom

   Dynamic interaction for SOCom method invocation, event subscription and publication, field notification, and zero-copy event transport across ``gateway_ipc_binding`` over LoLa shared memory.

   .. uml:: models/interaction_diagram_event_communication.puml
      :align: center
      :caption: Interaction diagram: Event communication

   .. uml:: models/zero_copy_event_update.puml
      :align: center
      :caption: Interaction diagram: Zero-copy event update across someipd and gatewayd

   .. uml:: models/interaction_diagram_method_communication.puml
      :align: center
      :caption: Interaction diagram: Method communication

Method Communication
^^^^^^^^^^^^^^^^^^^^

Methods calls are routed 1:1 from client to server application.
If a method reply is requested, the calling client is served with the related reply.
Independent method calls share no state.

Event Communication
^^^^^^^^^^^^^^^^^^^

Event communication follows a subscription-driven lifecycle between client and server connectors:

- **Subscription**: When the first client subscribes to an event, SOCom notifies the server via the event subscription change callback (``Event_state::subscribed``). While no client is subscribed, the server does not send event updates.
- **Zero-copy publication**: For zero-copy event updates, the server requests a ``Writable_payload`` from the subscribed client before writing the contiguous SOME/IP message (header + payload) and publishing the update.
- **Unsubscription**: After the last subscriber unsubscribes, SOCom notifies the server that the event is no longer subscribed (``Event_state::unsubscribed``).

Field Notification Communication
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A field notification is implemented as an event with an initial event update on subscription:

- **Initial value request**: Because SOCom does not cache event values, it requests an event update from the server for each new subscriber.
- **Targeted response**: The server answers with ``update_requested_event()``, which delivers the initial value only to subscribers with a pending initial update.
- **Implicit fulfillment**: A regular ``update_event()`` broadcast updates all subscribed clients and also satisfies any pending initial-update requests.

.. uml:: models/interaction_diagram_field_notification_communication.puml
   :align: center
   :caption: Interaction diagram: Field notification communication

Service Discovery and Bridging
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. comp_arc_dyn:: SOCom Service Discovery and Bridging
   :id: comp_arc_dyn__socom_design__discovery
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__socom

   Dynamic interaction for in-process service discovery and cross-process service bridge registration (``request_service`` and ``on_service_state_change``).

   .. uml:: models/interaction_diagram_service_gateway_require_service.puml
      :align: center
      :caption: Interaction diagram: Service Gateway - require service

   .. uml:: models/interaction_diagram_service_gateway_provide_service.puml
      :align: center
      :caption: Interaction diagram: Service Gateway - provide service

Service Gateway - require service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The creation of client connectors is forwarded to bridges as request_service() calls.
Service bridges look up the required service instance within their domain.
If available, they connect to the remote counterpart and locally create a proxy server connector (forwarding all communication).

Service Gateway - provide service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The creation of server connectors is found by service bridges using the client connector Service_state_change_callback.
Service bridges provide this information within their domain.
If any domain partner requests the service instance, the service bridge forwards all communication using this proxy client connector.

Deadlock detection
------------------

SOCom detects self-destruction deadlocks when a connector callback attempts to destroy its owning ``Client_connector`` or ``Server_connector``:

- Before invoking a callback on a ``Client_connector`` or ``Server_connector``, SOCom records the caller's thread ID.
- When the callback returns normally, the recorded thread ID is removed.
- If the connector destructor is invoked while the current thread's ID is still recorded as executing a callback on that connector, SOCom detects the self-deadlock, logs an error message to ``std::cerr``, and terminates the process via ``std::abort()``.

Client_connector deadlocks
---------------------------

When a running callback destroys the calling Client_connector a deadlock will happen, which will cause the application to be terminated.

The deadlock is caused because the Client_connector destructor waits for the callback to return.
The callback on the other hand waits for the destructor to return.

.. uml:: models/interaction_diagram_client_connector_deadlocks.puml
   :align: center
   :caption: Interaction diagram: Client_connector deadlocks

Server_connector deadlocks
---------------------------

When a running callback destroys the calling Server_connector a deadlock will happen, which will cause the application to be terminated.

The deadlock is caused because the Server_connector destructor waits for the callback to return.
The callback on the other hand waits for the destructor to return.

.. uml:: models/interaction_diagram_server_connector_deadlocks.puml
   :align: center
   :caption: Interaction diagram: Server_connector deadlocks
