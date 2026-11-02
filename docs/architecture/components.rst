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

- :need:`comp__gatewayd`
- :need:`comp__serializer`
- :need:`comp__null_serializer`
- :need:`comp__someipd`


..
   # Currently disabled because of issue eclipse-score/docs-as-code#580
   .. logic_arc_int:: Serializer Interface
      :id: logic_arc_int__someip__serializer
      :security: NO
      :safety: ASIL_B
      :status: valid

      The interface between the :need:`comp__gatewayd` and the serializer component.
      The gateway daemon uses this interface to serialize and deserialize SOME/IP messages.
