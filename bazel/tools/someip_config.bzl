# *******************************************************************************
# Copyright (c) 2025 Contributors to the Eclipse Foundation
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

"""Macros to generate and validate a SOME/IP gateway configuration"""

load("@score_communication//bazel/tools:json_schema_validator.bzl", "validate_json_schema_test")

def _get_expected_binary_name(json):
    """Extracts the expected binary filename from a JSON label.

    Args:
        json: The input JSON label.

    Returns:
        The expected binary filename (e.g., "config.bin").
    """
    json_label_str = str(json)
    json_basename = json_label_str.split("/")[-1].split(":")[-1]
    if not json_basename.endswith(".json"):
        json_basename = json_label_str.split(":")[-1]  # in case that json might be just a filename

    return json_basename.replace(".json", ".bin")

def _get_output_dir(output):
    """Validates output basename and extracts the output directory.

    Args:
        output: The output path string.

    Returns:
        The output directory path.
    """
    if "/" in output or ":" in output:
        path_part = output.split(":")[-1]
        if "/" in path_part:
            output_dir = "/".join(path_part.split("/")[:-1])
        else:
            output_dir = "."
    else:
        output_dir = "."

    return output_dir

def _generate_someip_config_bin_impl(name, json, output, **kwargs):
    """Generates a SOME/IP Gateway configuration binary based on a specified json file."""

    # automatically generate a test for binary generation
    validate_someip_config_test(
        name = name + "_test",
        json = json,
        expected_failure = False,
        tags = ["lint"],
    )

    schema_file_path = "@score_someip_gateway//src/gatewayd:etc/gatewayd_config.fbs"

    expected_file_name = _get_expected_binary_name(json)

    output_basename = output.split("/")[-1].split(":")[-1]
    if not output_basename.endswith(".bin"):
        _warn("'output' should end with '.bin'")

    output_dir = _get_output_dir(output)

    commands = [
        "$(location @flatbuffers//:flatc) --binary -o $(RULEDIR)/%s $(SRCS)" % output_dir,
    ]

    if expected_file_name != output_basename:
        commands.append("&& mv $(@D)/%s $(@)" % expected_file_name)

    native.genrule(
        name = name,
        srcs = [
            schema_file_path,
            json,
        ],
        outs = [output],
        cmd = " ".join(commands),
        message = "Generates a SOME/IP Gateway configuration binary based on a specified json file.",
        tools = ["@flatbuffers//:flatc"],
        **kwargs
    )

generate_someip_config_bin = macro(
    implementation = _generate_someip_config_bin_impl,
    inherit_attrs = native.genrule,  # type: ignore
    attrs = {
        "json": attr.label(
            doc = "The input JSON configuration file.",
            mandatory = True,
            allow_single_file = True,
            configurable = False,
        ),
        "output": attr.string(
            doc = "The output path (relative or absolute). Must end with '.bin'.",
            mandatory = True,
            configurable = False,
        ),
        "outs": None,
        "srcs": None,
        "cmd": None,
        "tools": None,
        "message": None,
    },
    doc = "Generates a SOME/IP Gateway configuration binary based on a specified json file.",
)

def _validate_someip_config_impl(name, json, expected_failure, **kwargs):
    """Validates a SOME/IP gateway config json against its corresponding schema."""

    validate_json_schema_test(
        name = name,
        json = json,
        schema = "@score_someip_gateway//src/gatewayd:config_schema.generate",
        expected_failure = expected_failure,
        **kwargs
    )

validate_someip_config_test = macro(
    doc = "Validates a SOME/IP gateway config json against its corresponding schema.",
    implementation = _validate_someip_config_impl,
    inherit_attrs = validate_json_schema_test,  # type: ignore
    attrs = {
        "json": attr.label(
            doc = "The input JSON configuration file.",
            mandatory = True,
            allow_single_file = True,
            configurable = False,
        ),
        "expected_failure": attr.bool(default = False),
        "schema": None,
    },
)

# buildifier: disable=print
def _warn(msg):
    print("{red}{msg}{nc}".format(red = "\033[0;31m", msg = "WARNING: " + msg, nc = "\033[0m"))
