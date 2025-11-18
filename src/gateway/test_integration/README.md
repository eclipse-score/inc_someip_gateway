# Integration tests

[`test_someip_gateway.sh`](test_someip_gateway.sh) runs the SOME/IP Gateway with stubs of the [`network access`](someip_plugin/) and [`payload transformation`](payload_transformation_plugin/) plugin.
The [`ipc_test_client`](ipc_test_client/) receives the event, which is send by the `network access` plugin.

The data flow for events is as follows:

```
someip_plugin -> socom -> payload transformation -> lola -> ipc_test_client
```
