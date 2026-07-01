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

"""Integration test macro with QEMU-only backends."""

load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("@score_itf//:defs.bzl", "py_itf_test")
load("@score_rules_imagefs//rules/qnx:ifs.bzl", "qnx_ifs")

def _extend_list_in_kwargs(kwargs, key, values):
    kwargs[key] = kwargs.get(key, []) + values
    return kwargs

def _extend_list_in_kwargs_without_duplicates(kwargs, key, values):
    kwargs_values = kwargs.get(key, [])
    for value in values:
        if value not in kwargs_values:
            kwargs_values.append(value)
    kwargs[key] = kwargs_values
    return kwargs

def integration_test(name, srcs, filesystem, **kwargs):
    """Creates an integration test target with QEMU-only backends.

    On Linux, tests run only when the `linux_qemu` flag is selected.
    On QNX, tests run with QEMU.
    The test can be configured to use a custom QEMU image and config when running with QEMU.

    The network setup makes using linux-sandbox mandatory.
    Otherwise no parallelism can be achieved.

    Args:
        name: The target name.
        srcs: Test source files.
        filesystem: Tests and dependencies to be run. Will be added / uploaded into the OS image. The entrypoint is a py_test compatible python file. Must be created using `pkg_files()`.
        **kwargs: Additional arguments passed to py_itf_test.
    """
    LINUX_TARGET_COMPATIBLE_WITH = [
        "@platforms//os:linux",
    ]

    # --- Linux QEMU artifacts ---
    filesystem_tar = "_qemu_filesystem_{}".format(name)
    pkg_tar(
        name = filesystem_tar,
        srcs = [filesystem],
    )

    linux_qemu_config = Label("//quality/integration_testing/environments/ubuntu24_04_qemu:qemu_config")
    linux_qemu_image = Label("@ubuntu_24_04_cloudimg//file")
    linux_qemu_seed_iso = Label("//quality/integration_testing/environments/ubuntu24_04_qemu:seed_iso")

    # --- QNX QEMU artifacts ---
    QNX_TARGET_COMPATIBLE_WITH = [
        "@platforms//cpu:x86_64",
        "@platforms//os:qnx",
    ]

    qemu_image = "_init_ifs_{}".format(name)
    qnx_ifs(
        name = qemu_image,
        out = "init_ifs_{}".format(name),
        build_file = "//quality/integration_testing/environments/qnx8_qemu:init_build",
        srcs = [filesystem, "//quality/integration_testing/environments/qnx8_qemu:qnx_config"],
        target_compatible_with = QNX_TARGET_COMPATIBLE_WITH,
    )

    qnx_qemu_config = Label("//quality/integration_testing/environments/qnx8_qemu:qemu_config")

    # --- Wire up data deps and args based on platform + linux_backend flag ---
    _extend_list_in_kwargs(kwargs, "data", select({
        "@platforms//os:qnx": [qemu_image, qnx_qemu_config],
        "//quality/integration_testing/flags:linux_qemu": [
            filesystem_tar,
            linux_qemu_config,
            linux_qemu_image,
            linux_qemu_seed_iso,
        ],
        "//conditions:default": [],
    }))
    _extend_list_in_kwargs(
        kwargs,
        "args",
        select({
            "@platforms//os:qnx": [
                "--log-cli-level=DEBUG",
                "--qemu-config=$(location {})".format(qnx_qemu_config),
                "--qemu-image=$(location {})".format(qemu_image),
            ],
            "//quality/integration_testing/flags:linux_qemu": [
                "--log-cli-level=DEBUG",
                "--qemu-config=$(location {})".format(linux_qemu_config),
                "--qemu-image=$(location {})".format(linux_qemu_image),
                "--qemu-seed-iso=$(location {})".format(linux_qemu_seed_iso),
                "--qemu-filesystem-tar=$(location {})".format(filesystem_tar),
            ],
            "//conditions:default": [],
        }),
    )

    # Integration tests are only executable in Linux QEMU mode or on QNX.
    _extend_list_in_kwargs(
        kwargs,
        "target_compatible_with",
        select({
            "@platforms//os:qnx": QNX_TARGET_COMPATIBLE_WITH,
            "//quality/integration_testing/flags:linux_qemu": LINUX_TARGET_COMPATIBLE_WITH,
            "//conditions:default": ["@platforms//:incompatible"],
        }),
    )

    # Tests spin up docker or qemu which requires a significant amount of system resources.
    if "size" not in kwargs:
        kwargs["size"] = "enormous"

    # While we require a significant amount of system resources, the tests are still short.
    if "timeout" not in kwargs:
        kwargs["timeout"] = "short"

    _extend_list_in_kwargs_without_duplicates(
        kwargs,
        "tags",
        [
            # QEMU networking requires TAP interfaces, which need CAP_NET_ADMIN.
            # Thus have root privileges inside the sandbox.
            "requires-fakeroot",
            # Enforce isolated network namespaces for sandboxed tests (linux-sandbox), required by fixed network ports
            "block-network",
        ],
    )

    py_itf_test(
        name = name,
        srcs = srcs,
        plugins = select({
            "@platforms//os:qnx": [
                "@score_itf//score/itf/plugins:qemu_plugin",
            ],
            "//quality/integration_testing/flags:linux_qemu": [
                "//quality/integration_testing/plugins/linux_qemu:linux_qemu_plugin",
            ],
            "//conditions:default": [],
        }),
        env = {"DOCKER_HOST": ""},
        **kwargs
    )
