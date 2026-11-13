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

"""
Creates "validate_json_schema_test" test rule which validates the input "json" file against its "schema"
"""

def _impl(ctx):
    # The script writes its own JUnit XML so the test case carries the
    # TestType and DerivationTechnique properties required by docs-as-code.
    script = """#!/usr/bin/env bash
    readonly expected_failure={expected_failure}
    write_xml() {{
        test -n "$XML_OUTPUT_FILE" || return 0
        local failure=""
        if test "$1" -ne 0; then
            failure='<failure message="JSON schema validation test failed"/>'
        fi
        cat > "$XML_OUTPUT_FILE" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<testsuites>
  <testsuite name="{test_name}" tests="1" failures="$1" errors="0">
    <testcase name="{test_name}" status="run" time="0">
      <properties>
        <property name="TestType" value="{test_type}"/>
        <property name="DerivationTechnique" value="{derivation_technique}"/>
      </properties>
      $failure
    </testcase>
  </testsuite>
</testsuites>
EOF
    }}
    '{validator}' '{schema}' < '{json}'
    readonly ret=$?
    if test "$expected_failure" = true && test "$ret" -ne 0; then
        write_xml 0
        exit 0
    elif test "$expected_failure" = false && test "$ret" -eq 0; then
        write_xml 0
        exit 0
    fi
    echo "Test Failed: Json validator performed json validation of '{json}' against '{schema}' schema with exit code $ret, meanwhile rule attribute 'expected_failure' is equal to '{expected_failure}'.
This means in case expected_failure=false then expected exit code should be zero (valid json). Otherwise, if expected_failure=true then exit code is non-zero (invalid json).
Note: by default expected_failure is false."
    write_xml 1
    exit 1
    """.format(
        validator = ctx.attr._validator.files_to_run.executable.short_path,
        schema = ctx.file.schema.short_path,
        json = ctx.file.json.short_path,
        expected_failure = "true" if ctx.attr.expected_failure else "false",
        test_name = ctx.label.package + "/" + ctx.label.name,
        test_type = ctx.attr.test_type,
        derivation_technique = ctx.attr.derivation_technique,
    )

    ctx.actions.write(
        output = ctx.outputs.executable,
        content = script,
        is_executable = True,
    )

    # To ensure the files needed by the script are available, put them in the runfiles
    runfiles = ctx.runfiles(files = [ctx.file._validator, ctx.file.json, ctx.file.schema])
    return [DefaultInfo(runfiles = runfiles)]

validate_json_schema_test = rule(
    implementation = _impl,
    attrs = {
        "derivation_technique": attr.string(
            default = "design-analysis",
            doc = "Recorded as the DerivationTechnique property in the test XML.",
        ),
        "expected_failure": attr.bool(default = False),
        "json": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "schema": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "test_type": attr.string(
            default = "interface-test",
            doc = "Recorded as the TestType property in the test XML.",
        ),
        "_validator": attr.label(
            default = Label("@json_schema_validator"),
            allow_single_file = True,
            executable = True,
            cfg = "exec",
        ),
    },
    test = True,
)
