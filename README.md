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
bazel run //src/gatewayd:gatewayd_example
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


## 📝 Configuration

### Gatewayd Config Schema Validation

The `gatewayd` module is configured using a flatbuffer binary file generated from a JSON file. We provide a JSON schema which helps when editing the JSON file, and can also be used to validate it.

#### Configuration Schema

The JSON schema for the `gatewayd` configuration is located at:

```bash
src/gatewayd/etc/gatewayd_config.schema.json
```

This schema defines the expected properties, data types, and constraints for a valid `gatewayd_config.json` configuration file.

#### Generate Configuration Binary

To generate a someip config binary for your project, add the following to your `BUILD.bazel` file:

```bash
load("@score_someip_gateway//bazel/tools:someip_config.bzl", "generate_someip_config_bin")
generate_someip_config_bin(
    name = "<generation_rule_name>",
    json = "//<package>:<path_to_gatewayd_config_json>",
    output = "etc/gatewayd_config.bin",
)
```

You can then either use it as a runfile dependency for a run target:

```bash
generate_someip_config_bin(
    name = "someipd_config",
    ...
)

native_binary(
    name = "gatewayd",
    src = "@score_someip_gateway//src/gatewayd",
    args = [
        "-service_instance_manifest",
        "$(rootpath etc/mw_com_config.json)",
    ],
    data = [
        "etc/mw_com_config.json",
        ":someipd_config",
    ],
)
```

Or you can manually generate the `gatewayd_config.bin` with the following command:

```bash
bazel build //:someipd_config # if the macro has been added to root BUILD.bazel
```

On success you can retrieve the generated `gatewayd_config.bin` from `bazel-bin/`. Check the success message for the exact path.


#### Configuration Validation

When using the `generate_someip_config_bin` macro a validation test is automatically generated to validate the schema json against the schema. This can be executed via:

```bash
bazel test //:<generation_rule_name>_test # if the macro has been added to root BUILD.bazel
```


## QNX Build

Either use a `.netrc` file to provide the login credentials for your myQNX account or provide them as environment variables `SCORE_QNX_USER` and `SCORE_QNX_PASSWORD`.
You can use an extension like `pomdtr.secrets` to manage the secrets or inject it via environment.

The QNX toolchain is automatically downloaded when building for QNX.
If the automatic download via bazel fails for some reason you can also provide the manually downloaded file in a directory which you then pass via the `--distdir` command line option.

Make sure your qnx license file is available as `/opt/score_qnx/license/licenses` (e.g. by copying it from your `~/.qnx/license/licenses`)

If you use a license server then add the following in in your `~/.bazelrc`:

    common --action_env=QNXLM_LICENSE_FILE=<port>@<license_server_host>

> :warning: Getting license from server not yet supported within devcontainer. Need to figure out how to adjust user & hostname properly.

### QNX x86_64 Testing

To run the tests for QNX on x86_64, follow these steps:

1.  **Build the QEMU image:**

    ```sh
    bazel build //deployment/qemu:run_qemu --config=x86_64-qnx
    ```

2.  **Run the QEMU image:**

    ```sh
    bazel run //deployment/qemu:run_qemu --config=x86_64-qnx
    ```

3.  **Run a basic test to check networking**

    ```sh
    bazel test //tests/integration:test_ssh --test_output=all --cache_test_results=no
    ```

4.  **Run unit tests:**

    ```sh
    bazel test //tests/UT:test_ut --test_output=all --cache_test_results=no
    ```
