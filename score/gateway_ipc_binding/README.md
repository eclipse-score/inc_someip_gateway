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

# Gateway IPC Binding

Gateway IPC Binding bridges SOCom services between ``gatewayd`` and ``someipd`` over ``mw::com``
(LoLa). The implementation uses generic proxies and skeletons for bridged service events and a
typed ``SomeipdService`` for peer liveness and configuration exchange.

## Design

[The design](docs/mw_com_binding.rst) describes the behavior and architecture of the Gateway IPC
Binding.
