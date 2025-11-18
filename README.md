# SOME/IP Gateway

The SOME/IP Gateway adds SOME/IP network access to S-CORE.
It bridges SOME/IP services from and to IPC and translates user defined types.

The SOME/IP Gateway uses a plugin architecture to realize SOME/IP network access and payload transformation.
The implementation tries to stick as much as possible to the [proposed architecture](https://eclipse-score.github.io/score/main/features/communication/some_ip_gateway/architecture/index.html).
Because the SOME/IP Gateway code shall be kept generic the IPC code was moved into the payload transformation plugin.
Otherwise it was not possible to keep the gateway code service agnostic.
The process split is not done yet, but has to be done eventually.

This is the code for [Elektrobits contribution](https://github.com/eclipse-score/score/issues/1830)

## Proof of concept

In its current state the code is a proof of concept to demonstrate how a SOME/IP Gateway can be implemented.
It only shows that reception of SOME/IP events at IPC is possible.
Methods and fields have been omitted because IPC does not support them yet.
Also E2E checks and actual binary payload transformation have been omitted due to time constraints.

---

## 📂 Project Structure

| File/Folder                         | Description                                       |
| ----------------------------------- | ------------------------------------------------- |
| `README.md`                         | Short description & build instructions            |
| `src/`                              | Source files for the module                       |
| `docs/`                             | Documentation (Doxygen for C++ / mdBook for Rust) |
| `.github/workflows/`                | CI/CD pipelines                                   |
| `.vscode/`                          | Recommended VS Code settings                      |
| `.bazelrc`, `MODULE.bazel`, `BUILD` | Bazel configuration & settings                    |
| `project_config.bzl`                | Project-specific metadata for Bazel macros        |
| `LICENSE.md`                        | Licensing information                             |
| `CONTRIBUTION.md`                   | Contribution guidelines                           |

---

## 🚀 Getting Started

### 1️⃣ Clone the Repository

```sh
git clone https://github.com/eclipse-score/inc_someip_gateway.git
cd inc_someip_gateway
```

### 2️⃣ Build the Examples of module

To build all targets of the module the following command can be used:

```sh
bazel build //src/...
```

This command will instruct Bazel to build all targets that are under Bazel
package `src/`.

If you are only interested in the SDK for building a SOME/IP plugin, build the tar archive of the SDK:

```sh
bazel build //src:inc_gateway_sdk
cp bazel-bin/src/inc_gateway_sdk.tar .
```

`inc_gateway_sdk.tar` contains the libraries and headers needed to build a SOME/IP plugin.
It does not provide any build system integration yet.

### 3️⃣ Run Tests

```sh
bazel test //...
```

The [gateway tests](src/gateway/test_integration) use a stub plugin, which sends events unconditionally.
You can log at the complete logs of their execution with

```sh
bazel test //src/gateway/test_integration/... --test_output=all
```

### 4 Running the SOME/IP Gateway

After you have written and compiled a SOME/IP network plugin you can run the gateway with the following command:

```sh
someip_gateway \
  -s /tmp/mw_com_lola_service_manifest.json \
  --cycle-time 10 \
  --num-cycles 0 \
  --plugin-path path/to/libsomeip-plugin-20-11.so \
  --xsomeip-manifest-dir /path/to/someip/manifest/dir/ \
  --interface dummy0 \
  --address fd00::deaf
```

Please keep in mind that you will have to alter some parameters to match your environment and set it up.
