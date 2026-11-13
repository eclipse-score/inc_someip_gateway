<!-- ----------------------------------------------------------------------------
  Copyright (c) 2026 Contributors to the Eclipse Foundation

  See the NOTICE file(s) distributed with this work for additional
  information regarding copyright ownership.

  This program and the accompanying materials are made available under the
  terms of the Apache License Version 2.0 which is available at
  https://www.apache.org/licenses/LICENSE-2.0

  SPDX-License-Identifier: Apache-2.0
----------------------------------------------------------------------------- -->

# Open SOME/IP specification source

The documentation build reads the Open SOME/IP specification at pinned commit
`4860461802c23f41c6227befcb25cc80e7d68771`. The pin is declared in
`MODULE.bazel`; source links use the same commit in `docs/conf.py`.

No upstream `needs.json` is checked in. Bazel fetches the pinned source and
generates a metadata-only `needs.json` under its output tree, then uses it to
generate the Sphinx requirements page. The generated JSON contains requirement
IDs, selected attributes, document names, and source line numbers only. It does
not contain specification titles, headings, or requirement wording. Do not
copy upstream prose or titles into this repository: the specification is
licensed Community-Spec-1.0, not Apache-2.0.

    Upstream: https://github.com/some-ip-com/open-someip-spec
    Licence:  SOME/IP Community Specification License (SPDX: Community-Spec-1.0)
              (c) Technica Engineering GmbH — NOT Apache-2.0.

## Updating the pinned specification commit

From the repository root, run the update script with the full upstream Git SHA:

  python3 tools/extneeds/update_open_someip_spec.py COMMIT_SHA

Replace `COMMIT_SHA` with the full 40-character upstream commit.

The script updates the pin in both `MODULE.bazel` and `docs/conf.py`, then
generates the metadata, builds the documentation, and runs `//:docs_check`.
It stops on the first failure; the updated pins remain in the worktree for
review or correction. If the local build needs additional Bazel options, pass
each one with a repeated `--bazel-arg=...` option.

The generated metadata JSON and RST are Bazel outputs, not source files to
commit. The mirrored requirements land in
`_build/requirements/extspec/index.html`.
