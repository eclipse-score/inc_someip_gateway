..
   *******************************************************************************
   Copyright (c) 2026 Contributors to the Eclipse Foundation

   See the NOTICE file(s) distributed with this work for additional
   information regarding copyright ownership.

   This program and the accompanying materials are made available under the
   terms of the Apache License Version 2.0 which is available at
   https://www.apache.org/licenses/LICENSE-2.0

   SPDX-License-Identifier: Apache-2.0
   *******************************************************************************

SOME/IP Gateway Migration Sequence
==================================

The migration is proposed as seven changes. Together, they cover the
``gatewayd`` daemon, SOCom, and the SOME/IP binding. The sequence is a starting
point; component boundaries and the scope of each change need further
refinement.

1. **Add SOCom payload types**

   Define the payload view and allocation contract needed by the binding, with
   focused payload ownership tests.

2. **Add SOCom service identity and discovery**

   Add service/version identity and discovery behavior required by the
   gateway, with tests for the agreed behavior.

3. **Add the reduced SOCom runtime and registry**

   Migrate the service registration, event, callback, and lifecycle behavior
   required by the gateway. Add focused runtime tests.

4. **Define the daemon bridge API and configuration**

   Agree on the local bridge contract and configuration between the daemons.

5. **Add the bridge backend**

   Implement the provider/consumer event bridge and test its event paths.

6. **Add gateway configuration and serializer integration**

   Migrate the configuration and serializer behavior needed by the gateway,
   with focused tests.

7. **Integrate ``gatewayd``**

   Connect the gateway service adapters and executable to the migrated SOCom
   behavior and bridge. Verify startup and event paths.

Requirements, architecture/design, and associated tests should be refined
alongside these changes. Only the high-level migration blocks are agreed now;
refine the boundaries, ordering, and detailed test scope as ongoing migration
and cleanup work is completed.
