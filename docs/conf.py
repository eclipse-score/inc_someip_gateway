# *******************************************************************************
# Copyright (c) 2024 Contributors to the Eclipse Foundation
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

project = "SOME/IP Gateway"
project_url = "https://eclipse-score.github.io/inc_someip_gateway/"
version = "0.1"

extensions = ["score_sphinx_bundle"]

required_in_id = ["gatewayd", "socom", "gib"]

# Component requirements link upward to communication-module feature
# requirements (``feat_req__com__*``) owned by eclipse-score/communication.
# Those needs are not currently imported via ``needs_external_needs`` in this
# project, so sphinx-needs reports the links as unresolved. Suppress until the
# external-needs source is wired in.
suppress_warnings = ["needs.link_outgoing"]
