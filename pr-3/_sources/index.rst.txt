..
   # *******************************************************************************
   # Copyright (c) 2024 Contributors to the Eclipse Foundation
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

.. _some_ip_gateway_architecture:

SOME/IP Gateway Architecture (Prototype)
########################################

.. toctree::
   :titlesonly:

Overview
========

The prototype implements the basic structure as defined in the corresponding `feature request`_.
It acts as a regular ``mw::com`` IPC client on the one side and as a SOME/IP client on the other side.
Actual payload transformation is not yet realized, instead binary data (interpreted as ASCII text) is used for demonstration.
Although there is no actual payload transformation, the structure already foresees the "transformation plugin architecture" as defined in the  `feature request`_.

The two required `plugin interfaces`_ are realized with the `SOCom`_ library:

- ``Someip_network_plugin_interface``
- ``Payload_transformation_plugin_interface``

Both plugins and their implementations are oblivious to each other.
Decoupling is achieved with the ``SOCom`` library, which is a SOME/IP abstraction with a plugin API for bridges.

The SOME/IP plugin exists in two implementations,
one implementation is a `test mock`_ which simply sends the ASCII data ``Hello Gateway`` and the other implemention is an actual implementation with an AUTOSAR compatible SOME/IP stack.
The second implementation can be demonstrated but is not part of the source repository.

.. _feature request: https://eclipse-score.github.io/score/main/features/communication/some_ip_gateway/index.html
.. _test mock: ../src/gateway/test_integration/someip_plugin
.. _plugin interfaces: ../src/gateway/plugin_interface/include/score/gateway
.. _SOCom: ../src/socom

.. figure:: some_ip_gateway_prototype.drawio.svg
   :align: center
   :name: _some_ip_gateway_prototype

   General overview of the prototype of the SOME/IP Gateway

SOCom Details
=============

`SOCom`_ is a SOME/IP abstraction with a plugin API for bridges.
Via bridges it gains access to IPC or the network (SOME/IP in this case).
``SOCom`` treats payloads as binary blobs and supports events, methods and fields.

Provided services are represented as a `Server_connector <../src/socom/include/score/socom/server_connector.hpp>`__ and required services as a `Client_connector <../src/socom/include/score/socom/client_connector.hpp>`__.
The ``Server_connector`` can send event updates and the ``Client_connector`` can invoke methods or subscribe to events.
To support fields the ``Client_connector`` can request an immediate value at event subscription.
One ``Server_connector`` can serve many ``Client_connectors`` as long as the interface definition matches.

Bridges are registered with two callbacks at the `Runtime <../src/socom/include/score/socom/runtime.hpp>`__.
The SOME/IP plugin which implements the ``Someip_network_plugin_interface`` has to register itself as a bridge.

The ``Someip_network_plugin_interface`` implementation and the ``Payload_transformation_plugin_interface`` implementation communicate then via ``Client_connectors`` and ``Server_connectors``.

.. figure:: some_ip_gateway_prototype_socom_details.drawio.svg
   :align: center
   :name: _some_ip_gateway_prototype_socom_details

   SOCom utilization within SOME/IP Gateway prototype

Interaction with a SOME/IP daemon
=================================

The example shows a data flow from an AUTOSAR SOME/IP Daemon via the network and the Gateway to ``mw::com``.

.. figure:: reception_from_network.drawio.svg
   :align: center
   :name: _some_ip_gateway_reception_from_network

   Data flow from SOME/IP network to ``mw::com``
