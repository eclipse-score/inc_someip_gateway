#!/bin/bash

# *******************************************************************************
# Copyright (c) 2025 Elektrobit Automotive GmbH
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

set -euxo pipefail

common_location=$(find * -name common.sh)

. "$common_location"

# this cannot work if both sender and receiver expect 20 samples
# the sender has to send infinite samples and the receiver has to terminate after having received some
CPP_EXAMPLE_CMD="$(find * -name ipc_bridge_cpp) -s $MANIFEST_LOCATION --cycle-time 10"
RUST_EXAMPLE_CMD="$(find * -name ipc_bridge_rs) -s $MANIFEST_LOCATION --cycle-time 10"

# In initial setup gateway acts as sender
PLUGIN_PATH="$(find * -name libsomeip_network_plugin.so)"
GATEWAY_CMD="$(find * -name someip_gateway) --plugin-path $PLUGIN_PATH -s $MANIFEST_LOCATION --cycle-time 10"

setup

echo -e "\n\n\nRunning Rust receiver and gateway sender"
run_receiver_sender "$RUST_EXAMPLE_CMD" "$GATEWAY_CMD"

echo -e "\n\n\nRunning C++ receiver and gateway sender"
run_receiver_sender "$CPP_EXAMPLE_CMD" "$GATEWAY_CMD"
