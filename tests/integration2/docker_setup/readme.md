<!--
*******************************************************************************
Copyright (c) 2026 Contributors to the Eclipse Foundation

See the NOTICE file(s) distributed with this work for additional
information regarding copyright ownership.

This program and the accompanying materials are made available under the
terms of the Apache License Version 2.0 which is available at
https://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
*******************************************************************************
-->

Create the containers (you must be within the top level of the score-someip-gateway repo).

    ```bash
    docker compose --project-directory tests/integration/docker_setup/ build
    ```

Start the containers, using the entrypoint files.

    ```bash
    docker compose --project-directory tests/integration/docker_setup/ up
    ```
