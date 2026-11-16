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

"""Depend on someipd built with //score/someipd:stack=opensomeip.

The flag defaults to vsomeip. The transition is attached to this dependency
only, so `bazel test //...` builds the opensomeip binary for that one edge
and leaves gatewayd and the existing gatewayd_test on vsomeip.
"""

_STACK_FLAG = "//score/someipd:stack"

# The stack does not depend on the incoming configuration or on rule attributes.
def _opensomeip_stack_impl(_settings, _attr):
    return {_STACK_FLAG: "opensomeip"}

_opensomeip_stack_transition = transition(
    implementation = _opensomeip_stack_impl,
    inputs = [],
    outputs = [_STACK_FLAG],
)

def _someipd_opensomeip_impl(ctx):
    info = ctx.attr.binary[0][DefaultInfo]
    executable = info.files_to_run.executable
    runfiles = info.default_runfiles
    if runfiles == None:
        runfiles = ctx.runfiles(files = [executable])
    else:
        runfiles = runfiles.merge(ctx.runfiles(files = [executable]))
    return [DefaultInfo(files = depset([executable]), runfiles = runfiles)]

someipd_opensomeip = rule(
    implementation = _someipd_opensomeip_impl,
    attrs = {
        "binary": attr.label(
            cfg = _opensomeip_stack_transition,
            executable = True,
            mandatory = True,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
