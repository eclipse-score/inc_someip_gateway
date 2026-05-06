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
FROM ghcr.io/eclipse-score/devcontainer:v1.5.0

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
