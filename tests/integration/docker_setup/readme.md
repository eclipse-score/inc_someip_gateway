Create the containers (you must be within the top level of the score-someip-gateway repo).

    ```bash
    docker compose --project-directory tests/integration/docker_setup/ build
    ```

Start the containers, using the entrypoint files.

    ```bash
    docker compose --project-directory tests/integration/docker_setup/ up
    ```
