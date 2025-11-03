# *******************************************************************************
# Copyright (c) 2025 ETAS GmbH
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

FROM ubuntu:jammy
SHELL ["/bin/bash", "-xec"]
RUN export DEBIAN_FRONTEND=noninteractive;\
    apt-get update;\
    apt-get dist-upgrade --purge --yes\
    bind9-dnsutils\
    gdb\
    iptables\
    iproute2\
    libnetfilter-queue-dev\
    libnfnetlink-dev\
    lsof\
    net-tools\
    openssh-server\
    pkg-config\
    python3-pip\
    python3-scapy\
    tcpdump\
    socat\
    iputils-ping\
    strace\
    ;\
    apt-get autoremove --purge --yes;\
    apt-get clean;

# Pinned GCC version
ENV CC=gcc-12
ENV CXX=g++-12
ENV GCOV=gcov-12

# /etc configurations
RUN echo 'SendEnv *' > /etc/ssh/ssh_config.d/vsomeip.conf;\
    echo 'AcceptEnv *' > /etc/ssh/sshd_config.d/vsomeip.conf

# SSH configurations
RUN ssh-keygen -q -b 1024 -f ~/.ssh/id_rsa -t rsa -N '';\
    install -DTm0400 ~/.ssh/id_rsa.pub ~/.ssh/authorized_keys;\
    install -DTm0400 ~/.ssh/id_rsa /commonapi_main/lxc-config/.ssh/mgc_lxc/rsa_key_file.pub

# Add ssh_config to the container
COPY tests/integration/docker_setup/ssh_config /root/.ssh/config
RUN chown root:root /root/.ssh/config && chmod 600 /root/.ssh/config

# Place stuff in a known place
WORKDIR /home/source

# Declare bind-mounts
VOLUME ["/home/build", "/home/logs", "/home/source"]
