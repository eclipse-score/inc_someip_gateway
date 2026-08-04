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

# Integration Tests

Integration tests for testing network behavior of the SOME/IP Gateway.

## Network trace

For each test, a `.pcap` file is recorded in the test output directory, which contains the network traffic during the test execution. This can be used for debugging and verification of the expected network interactions.

> [!NOTE]
> For whatever reason `tcpdump` cannot be killed at any time and you may get a permission denied error.
> Thus it is terminated by terminating `linux-sandbox`.

## How to use

Each test must use the `clean_state` fixture or use one based on that.
The `clean_state` fixture checks that the environment has not been used by a previous tests and fails if it detects leftover state.
This ensures that tests do not interfere with each other and that the test environment is clean for each test.

```python
def test_example(clean_state):
    # test code here
```

This implies that there is only one `test_*` function per test file and only one test file per Bazel target.

```bazel
integration_test(
    name = "test_example",
    srcs = [
        "conftest.py",      # contains test fixture clean_state
        "test_example.py",
    ],
    ...
)
```
