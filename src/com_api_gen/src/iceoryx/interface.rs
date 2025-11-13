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
#![allow(unsafe_op_in_unsafe_fn)]
#![allow(unused_imports)]

use core::mem::ManuallyDrop;

use com_api::prelude::Reloc;
use gateway_common::e2e::E2ETypeConnector;
use iceoryx2::prelude::PlacementDefault;
use iceoryx2::prelude::ZeroCopySend;

#[derive(Clone, Debug)]
#[repr(C)]
pub struct WindowsPosition {
    pub fl: u8,
    pub fr: u8,
    pub rl: u8,
    pub rr: u8,
}

impl PlacementDefault for WindowsPosition {
    unsafe fn placement_default(ptr: *mut Self) {
        PlacementDefault::placement_default(&raw mut (*ptr).fl);
        PlacementDefault::placement_default(&raw mut (*ptr).fr);
        PlacementDefault::placement_default(&raw mut (*ptr).rl);
        PlacementDefault::placement_default(&raw mut (*ptr).rr);
    }
}
unsafe impl Reloc for WindowsPosition {}
unsafe impl ZeroCopySend for WindowsPosition {}

impl E2ETypeConnector for WindowsPosition {
    type ConnectedE2EProfile = gateway_common::e2e::E2ESomeUserProfile;
}

#[derive(Clone, Debug)]
#[repr(C)]
pub struct RainSensor {
    pub is_wet: bool,
}

impl PlacementDefault for RainSensor {
    unsafe fn placement_default(ptr: *mut Self) {
        PlacementDefault::placement_default(&raw mut (*ptr).is_wet);
    }
}
unsafe impl Reloc for RainSensor {}
unsafe impl ZeroCopySend for RainSensor {}

impl E2ETypeConnector for RainSensor {
    type ConnectedE2EProfile = gateway_common::e2e::E2ESomeUserProfile2;
}

#[derive(Clone, Debug)]
#[repr(C)]
pub struct CloseWindows {
    pub close: bool,
}

impl PlacementDefault for CloseWindows {
    unsafe fn placement_default(ptr: *mut Self) {
        PlacementDefault::placement_default(&raw mut (*ptr).close);
    }
}
unsafe impl Reloc for CloseWindows {}
unsafe impl ZeroCopySend for CloseWindows {}

impl E2ETypeConnector for CloseWindows {
    type ConnectedE2EProfile = gateway_common::e2e::E2ESomeUserProfile;
}
