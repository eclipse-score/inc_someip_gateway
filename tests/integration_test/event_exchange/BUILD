# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
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

# Portable mw::com event exchange application. The sources (header + main)
# depend only on mw::com public API and the serializer PreSerializedData
# helper; no dependency on someipd, gatewayd, socom, gateway_ipc_binding, or
# @vsomeip. Lifting this binary into the communication module requires only
# copying the directory and writing a new BUILD file that points at that
# module's mw::com target.

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")
load("@score_communication//bazel/tools:json_schema_validator.bzl", "validate_json_schema_test")
load("//bazel/tools:someip_config.bzl", "generate_someip_config_bin")

exports_files([
    "config/app_mw_com_config.json",
    "config/gatewayd_mw_com_config.json",
    "config/someipd_mw_com_config.json",
    "config/vsomeip.json",
    "config/mw_someip_config.json",
])

cc_library(
    name = "service_interface",
    hdrs = ["service_interface.h"],
    visibility = ["//visibility:private"],
    deps = [
        "//score/serializer:pre_serialized_data",
    ],
)

cc_binary(
    name = "event_exchange_app",
    srcs = ["event_exchange_app.cpp"],
    visibility = ["//tests/integration_test:__subpackages__"],
    deps = [
        ":service_interface",
        "@score_baselibs//score/mw/log",
        "@score_communication//score/mw/com",
    ],
)

# Generate the gatewayd flatbuffer binary from the single JSON source.
generate_someip_config_bin(
    name = "mw_someip_config_bin",
    json = "config/mw_someip_config.json",
    output = "mw_someip_config.bin",
    visibility = ["//tests/integration_test:__subpackages__"],
)

# Schema validation. Each per-process mw::com manifest is validated against the
# upstream schema; the mw_someip_config.json is validated by
# generate_someip_config_bin internally.
validate_json_schema_test(
    name = "app_mw_com_config_schema_valid",
    size = "small",
    json = "config/app_mw_com_config.json",
    schema = "@score_communication//score/mw/com:config_schema",
    tags = ["lint"],
)

validate_json_schema_test(
    name = "gatewayd_mw_com_config_schema_valid",
    size = "small",
    json = "config/gatewayd_mw_com_config.json",
    schema = "@score_communication//score/mw/com:config_schema",
    tags = ["lint"],
)

validate_json_schema_test(
    name = "someipd_mw_com_config_schema_valid",
    size = "small",
    json = "config/someipd_mw_com_config.json",
    schema = "@score_communication//score/mw/com:config_schema",
    tags = ["lint"],
)

# The vsomeip schema is a package-local copy of
# tests/tc8_conformance/config/tc8_vsomeip_config.schema.json. tc8_conformance
# does not export its schema and is frozen under the active plan, so a local
# copy avoids editing a frozen package. Update both if the schema evolves.
validate_json_schema_test(
    name = "vsomeip_config_schema_valid",
    size = "small",
    json = "config/vsomeip.json",
    schema = "config/vsomeip_config.schema.json",
    tags = ["lint"],
)

# Host-side SOME/IP + SD peer imported by test_event_exchange. Exposed as a
# py_library so the test can `from event_exchange.someip_peer import ...`.
py_library(
    name = "someip_peer",
    testonly = True,
    srcs = [
        "__init__.py",
        "someip_peer.py",
    ],
    imports = [".."],
    visibility = ["//tests/integration_test:__subpackages__"],
)
