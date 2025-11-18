/********************************************************************************
* Copyright (c) 2025 Contributors to the Eclipse Foundation
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
///! This is the "generated" part for the ipc_bridge proxy. Its main purpose is to provide the imports
///! of the type- and name-dependent part of the FFI and create the respective user-facing objects.

#[repr(C)]
// #[derive(Default)]
pub struct MapApiLanesStamped {
    pub string_data: [u8; 101],
}

impl Default for MapApiLanesStamped {
    fn default() -> Self {
        Self {
            string_data: [0; 101],
        }
    }
}

mw_com::import_interface!(mw_com_IpcBridge, IpcBridge, {
    map_api_lanes_stamped_: Event<crate::MapApiLanesStamped>
});

mw_com::import_type!(mw_com_MapApiLanesStamped, crate::MapApiLanesStamped);
