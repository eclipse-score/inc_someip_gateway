# gateway

The `gateway` loads plugins, which implement one of the interfaces defined by `plugin_interface`.

## gateway

The [`gateway`](gateway/) bridges SOME/IP on the network and IPC.
It loads the SOME/IP network access and payload transformation as separate plugins.
The plugin approach allows to keep the `gateway` code generic and to support multiple SOME/IP implementations.
For payload transformation plugins are also required to support user defined datatypes.

### Loading of plugins

One goal was to have plugins which are either statically or dynamically linked with a unified interface.
This increases the flexibility for deployments.

#### SOME/IP network access

Only a single plugin is loaded via `dlopen()` to stay flexible after building the code.
It can be specified with `--plugin-path`.

#### Payload transformation

These plugins are statically linked.
At the moment loading them via `dlopen()` is not possible due to the use of static variables (e.g. the `mw::com` Runtime).

## plugin_interface

[`plugin_interface`](plugin_interface/) defines the interfaces for the SOME/IP network access and payload transformation plugins.

One goal was to have plugins which are either statically or dynamically linked with a unified interface.
This increases the flexibility for deployments.

#### SOME/IP network access

This plugin opens network socket to deal with SOME/IP and SOME/IP-SD.
It reads SOME/IP manifests which specify which services to provide or to require and their respective network configuration.

#### Payload transformation

Transforms payload between SOME/IP and IPC binary representation.
It also sends or receives the data to or from IPC.
