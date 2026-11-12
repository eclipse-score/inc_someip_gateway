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

required_in_id = ["gatewayd", "socom", "gib", "someip", "gateway", "extspec"]

# Link source-only Open SOME/IP references to the pinned upstream revision.
SPEC_REPO = "https://github.com/some-ip-com/open-someip-spec"
SPEC_COMMIT = "4860461802c23f41c6227befcb25cc80e7d68771"

needs_string_links = {
    "spec_ref": {
        "regex": r"^(?P<doc>[a-z0-9-]+):(?P<line>\d+)$",
        "link_url": f"{SPEC_REPO}/blob/{SPEC_COMMIT}/src/{{{{doc}}}}.rst#L{{{{line}}}}",
        "link_name": "{{doc}}.rst:{{line}}",
        "options": ["hash"],
    },
}

# Existing component requirements also reference communication-module feature
# requirements (``feat_req__com__*``), which are not part of the Open SOME/IP
# import. Keep suppressing unresolved links for those external references.
suppress_warnings = ["needs.link_outgoing"]
