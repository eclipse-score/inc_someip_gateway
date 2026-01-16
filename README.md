
# SOME/IP Gateway

The gateway is divided into a gateway daemon (gatewayd) which contains the network-independent logic (payload serialization, etc.) and the SOME/IP daemon (someipd) which binds to the concrete SOME/IP stack.
The IPC interface between the gatewayd and the someipd serves as isolation boundary between ASIL and QM context and also allows to replace the network stack without touching the main gateway logic.

![SOME/IP Gateway Architecture](docs/architecture/score-someip-car-window-overview.drawio.png)


---

## 🚀 Getting Started

### Clone the Repository

```sh
git clone https://github.com/eclipse-score/inc_someip_gateway.git
cd inc_someip_gateway
```

### Start the daemons

Start the daemons in this order:

```sh
bazel run //src/gatewayd
```

and in a separate terminal

```sh
bazel run //src/someipd
```

### Run Example app

```sh
bazel run //examples/car_window_sim:car_window_controller
```

If you type `open` or `close` the command will be sent via network.


### Dockerized integration test POC

For integration tests, a docker based approach was taken.
As a proof of concept `docker compose` can be used to build, setup and run the containers.
In the future a pytest based setup can be implemented to orchestrate the containers.

Build the docker containers:

```sh
docker compose --project-directory tests/integration/docker_setup/ build
```

Start up the containers:

```sh
docker compose --project-directory tests/integration/docker_setup/ up
```

Those containers are pre-configured (IP adresses, multicast route, ...).
The someipd-1 container already starts up the `gatewayd` and the `someipd`.

In Wireshark the network traffic can be seen by capturing on `any` with `ip.addr== 192.168.87.2 || ip.addr ==192.168.87.3`.

On the client side, start up the `sample_client` in another shell:

```sh
docker exec -it --env VSOMEIP_CONFIGURATION=/home/source/tests/integration/sample_client/vsomeip.json docker_setup-client-1 /home/source/bazel-bin/tests/integration/sample_client/sample_client
```

Finally start the benchmark on the someipd-1 container in a third shell:

```sh
docker exec -it docker_setup-someipd-1 /home/source/bazel-bin/tests/performance_benchmarks/ipc_benchmarks
```

### Gatewayd Config Schema Validation

#### 📝 Configuration

The `gatewayd` module uses a JSON file for configuration. To ensure the validity and structure of this configuration, a JSON schema is provided.

#### Configuration Schema

The JSON schema for the `gatewayd` configuration is located at:

```
/src/gatewayd/etc/gatewayd_config.schema.json
```

This schema defines the expected properties, data types, and constraints for a valid `gatewayd_config.json` configuration file.

#### Configuration Validation

You can validate your JSON configuration file against the schema using the `validate_json_schema_test` Bazel rule. This test will ensure that your configuration file is compatible with `gatewayd`.

To add a validation test to your project, add the following to your `BUILD.bazel` file:

```bazel
# Assuming 'validate_json_schema_test' is available in your project
# and you have a config file named 'gatewayd_config.json'

validate_json_schema_test(
    name = "my_gatewayd_config_validation_test",
    schema = "//src/gatewayd/etc:gatewayd_config.schema.json",
    json = "gatewayd_config.json",
)
```

**Note**: You will need to replace `gatewayd_config.json` with the actual paths to your configuration file, respectively.

To run the validation test, execute the following command from the root of your workspace:

```bash
bazel test //<my_project>:<my_gatewayd_config_validation_test>
```

If the test passes, your configuration file is valid. If it fails, the test logs will provide details about the validation errors.
