// Copyright (c) 2025 Contributors to the Eclipse Foundation
//
// See the NOTICE file(s) distributed with this work for additional
// information regarding copyright ownership.
//
// This program and the accompanying materials are made available under the
// terms of the Apache License Version 2.0 which is available at
// <https://www.apache.org/licenses/LICENSE-2.0>
//
// SPDX-License-Identifier: Apache-2.0
//
use std::path::PathBuf;

use abi_types_codegen::{
    build,
    config::{Config, RustOptions, Target},
};

fn main() {
    let source: PathBuf = ["src", "iceoryx", "interface.types"].iter().collect();
    println!("cargo::rerun-if-changed={}", source.display());

    let config = Config {
        format: true,
        source,
        output: None,
        target: Target::Rust,
        rust: RustOptions::iceoryx2(),
    };
    build(&config).unwrap();
}
