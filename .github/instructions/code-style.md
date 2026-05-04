<!--
*******************************************************************************
Copyright (c) 2026 Contributors to the Eclipse Foundation

See the NOTICE file(s) distributed with this work for additional
information regarding copyright ownership.

This program and the accompanying materials are made available under the
terms of the Apache License Version 2.0 which is available at
https://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
*******************************************************************************
-->
---
applyTo: "**/*.{cpp,h,cc,cxx,hpp,rs,py}"
---

# Code Style & Conventions

## License Headers (Required)

**All source files must include Apache 2.0 license headers.** Always ensure the header is present when editing a file, even if it was previously missing.

```cpp
/********************************************************************************
 * Copyright (c) <year> Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
```

## C++ Style

- Namespace organization: `score::someip_gateway::gatewayd`
- Configuration via FlatBuffers
- RAII patterns for resource management

## Style Application Rules

- **New files**: Follow prescribed conventions above.
- **Existing files**: Match existing style, even if it differs from conventions.
