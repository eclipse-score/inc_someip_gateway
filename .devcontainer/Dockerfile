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

# Use Dockerfile to get dependabot version bumps after new image is released
FROM ghcr.io/eclipse-score/devcontainer:v1.2.0

# Set the value if a proxy is used: cntlm, Px, etc.Example: BUILD_PROXY = http://host.docker.internal:<port>
ARG BUILD_PROXY=""

# Install networking and QEMU tools (conditionally using proxy if provided)
RUN if [ -n "$BUILD_PROXY" ]; then \
        export http_proxy="${BUILD_PROXY}"; \
        export https_proxy="${BUILD_PROXY}"; \
    fi; \
    apt-get update \
    && apt-get install -y --no-install-recommends \
        software-properties-common \
    && add-apt-repository -y universe \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        bridge-utils \
        iproute2 \
        iputils-ping \
        iptables \
        libcap2-bin \
        qemu-system-x86 \
        tcpdump

# Build argument for dynamic username (defaults to 'vscode' if not provided)
ARG USERNAME=vscode

# RENAME 'vscode' user to the specified username to satisfy the QNX license
# We use usermod to change the login name and home directory
RUN if [ "$USERNAME" != "vscode" ]; then \
        usermod -l ${USERNAME} vscode \
        && groupmod -n ${USERNAME} vscode \
        && usermod -d /home/${USERNAME} -m ${USERNAME} \
        && ln -s /home/${USERNAME} /home/vscode  \
        && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
        && chmod 0440 /etc/sudoers.d/${USERNAME}; \
    fi

# Set the default user for the container
USER ${USERNAME}
