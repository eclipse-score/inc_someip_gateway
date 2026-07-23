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

Components
==========

.. comp:: SOME/IP Gateway Daemon
   :id: comp__gatewayd
   :status: valid
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway

   Connects the :need:`comp__someipd` to the application via LoLa IPC (mw::com).


..
   # Currently disabled because of issue eclipse-score/docs-as-code#580
   .. logic_arc_int:: Serializer Interface
      :id: logic_arc_int__someip__serializer
      :security: NO
      :safety: ASIL_B
      :status: valid

      The interface between the :need:`comp__gatewayd` and the serializer component.
      The gateway daemon uses this interface to serialize and deserialize SOME/IP messages.


.. comp:: SOME/IP Serializer
   :id: comp__serializer
   :status: valid
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway

   Serializer and deserializer for SOME/IP messages. Used by the :need:`comp__gatewayd`.
   The serializer is built or configured especially for the application data types that it handles.


.. comp:: Null Serializer
   :id: comp__null_serializer
   :status: valid
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway

   A serializer that does not actually serialize or deserialize data, but only handles messages that are already serialized by the application.


.. comp:: SOME/IP Network Daemon
   :id: comp__someipd
   :status: valid
   :safety: QM
   :security: NO
   :belongs_to: feat__someip_gateway

   Handles the SOME/IP communication with the network.
