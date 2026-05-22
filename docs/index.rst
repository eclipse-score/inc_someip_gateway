..
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

SOMEIP Gateway Documentation
=============================


.. contents:: Table of Contents
   :depth: 2
   :local:

Overview
--------

TBD

Module Layout
--------------

.. code-block:: text

    someip_gateway/                     # Root folder of the module
    ├── .github/
    │   └── workflows/                  # CI/CD pipelines
    ├── docs/                           # Global documentation of the module
    │   ├── <TBD>
    ├── examples/                       # Usage examples
    ├── score/                          # Components of the module
    │   ├── tests/                      # Module-level tests (e.g., feature integration tests, system tests) [wp__verification_comp_int_test]
    │   └── <component_name>/           # Component folder for each component of the module
    │       ├── docs/                   # Documentation of the component
    │       │   ├── architecture/       # Component architecture [wp__component_arch]
    │       │   │                       #   (only if lower level components exist)
    |       |   |                       #   architecture review [wp__sw_arch_verification],
    │       │   ├── detailed_design/    # Detailed design [wp__sw_implementation]
    │       │   │                       #   code inspection [wp__sw_implementation_inspection]
    │       │   ├── requirements/       # Component requirements [wp__requirements_comp],[wp__requirements_inspect]
    │       │   ├── safety_analysis/    # Safety analysis [wp__sw_component_fmea], [wp__sw_component_dfa], [wp__requirements_comp_aou]
    |       |   |                       # Component classification [wp__sw_component_class] for pre-existing software
    │       │   │                       #   (only if component architecture exists)
    │       │   ├── security_analysis/  # Security analysis [wp__sw_component_security_analysis]
    │       │   │                       #   (only if component architecture exists)
    │       │   └── manuals/            # User documentation (of a single component, e.g., user manual of a library component, optional)
    │       └── src/                    # Source files, include files, unit tests [wp__verification_sw_unit_test],
    │           ├── <lower_level_comp>/ # Lower level component (follows <component_name> structure)
    │           └── tests/              # Component-level tests (e.g., unit tests) [wp__verification_sw_unit_test]
    ├── MODULE.bazel                    # Bazel module definition
    ├── BUILD                           # Root build rules
    ├── project_config.bzl              # Project metadata used by Bazel macros
    └── README.md                       # Entry point of the repository


Module / Feature Documentation
------------------------------

.. toctree::
   :maxdepth: 1

   requirements/index
   architecture/index.rst
   tc8_conformance/index.rst


Component documentation
-------------------------------

.. toctree::
   :maxdepth: 1

   socom/design/index.rst


Examples
--------

<TBD>


Quick Start
-----------

To build the module:

.. code-block:: bash

   bazel build //src/...

To run integration tests:

.. code-block:: bash

   bazel test //tests/...

Configuration
-------------

The `project_config.bzl` file defines metadata used by Bazel macros.

Example:

.. code-block:: python

   PROJECT_CONFIG = {
       "asil_level": "QM",
       "source_code": ["cpp", "rust"]
   }

This enables conditional behavior (e.g., choosing `clang-tidy` for C++ or `clippy` for Rust).
