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


### QEMU x86_64 - based integration test and unit tests

For integration tests where the communication between two QEMU instances is required, a custom implementation is used to start and manage the QEMU instances within the test logic. This is because ITF does not support starting multiple QEMU instances in parallel yet.

For the QEMU QNX x864 image to run on host please run the script deployment/qemu/setup_bridge.sh with sudo privileges to setup the required network bridge and tap interfaces.
It is stronly recommended to run all tests with `--nocache_test_results` which is the best way on development cycles to ensure you are always running the latest version of the tests and not accidentally running cached results.

Tu run all tests (will take around 2 minutes)

```sh
bazel test  //tests/...  --test_output=all --nocache_test_results --config=x86_64-qnx
```

Unit tests are defined by the bazel `test_ut` target:

```sh
bazel test //tests/UT:test_ut --test_output=all  --config=x86_64-qnx
```

For Integration SOMEIP Service Discovery tests:

```sh
bazel test //tests/integration/... --test_output=all --config=x86_64-qnx
```

Run a specific integration test `test_negative_only_qemu1_with_services` for SOMEIP Service Discovery test suite:

```sh
bazel test //tests/integration:test_someip_sd --test_output=all --config=x86_64-qnx --test_arg='-k' --test_arg='test_negative_only_qemu1_with_services'
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
