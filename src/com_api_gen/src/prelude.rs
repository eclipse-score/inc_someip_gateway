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

#[cfg(feature = "lola")]
pub use com_api_gen_lola::{Tire, VehicleInterface};

#[cfg(feature = "iceoryx")]
pub use crate::iceoryx::{
    CloseWindows, RainConsumer, RainInterface, RainOfferedProducer, RainProducer, RainSensor,
    WindowsConsumer, WindowsInterface, WindowsOfferedProducer, WindowsPosition, WindowsProducer,
};
