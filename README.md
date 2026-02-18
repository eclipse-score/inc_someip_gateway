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


### QEMU x86_64 - based integration test POC

For integration tests, a QEMU based approach was taken.
A pytest based setup has been implemented connect to the QEMU instances and run integration and unit tests.
For unit tests one QEMU instance is sufficient, for integration tests two instances are used to test the communication between two SOME/IP stacks via the gateway.A host bridge network is used to connect the QEMU instances with the host and with each other.

Build the QEMU images and the dependant c++ binaries/ libraries / configuration files. Any change will be automatically detected.

```sh
bazel build //deployment/qemu:someip_gateway_ifs --config=x86_64-qnx
```

The QEMU instances can be started manually if needed for debugging or development purposes.

```sh
bazel run //deployment/qemu:run_qemu_1 --config=x86_64-qnx
bazel run //deployment/qemu:run_qemu_2 --config=x86_64-qnx
```

SSH into each instance is available:
```sh
ssh root@192.168.87.2 -o StrictHostKeyChecking=no
ssh root@192.168.87.3 -o StrictHostKeyChecking=no
```

Those QEMU instances are pre-configured (IP addresses, multicast route, ...). The tests will start thhe required processes (gatewayd, someipd, example app) and then run the test logic.

The network traffic can be seen on the host via tcpdump on the host `virbr0` interface:
```sh
sudo tcpdump -i virbr0 -w someip_capture.pcap "host 192.168.87.2 or host 192.168.87.3"
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
