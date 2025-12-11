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
use std::default::Default;

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WindowState {
    Stopped = 0,
    Opening = 1,
    Closing = 2,
    Open = 3,
    Closed = 4,
}

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WindowCommand {
    Stop = 0,
    Open = 1,
    Close = 2,
}

impl Default for WindowState {
    fn default() -> Self {
        WindowState::Stopped
    }
}

impl Default for WindowCommand {
    fn default() -> Self {
        WindowCommand::Stop
    }
}

#[repr(C)]
#[derive(Default)]
pub struct WindowInfo {
    pub pos: u32,
    pub state: WindowState,
}

#[repr(C)]
#[derive(Default)]
pub struct WindowControl {
    pub command: WindowCommand,
}

mw_com::import_interface!(my_WindowInfoInterface, WindowInfoInterface, {
    window_info_: Event<crate::WindowInfo>
});

mw_com::import_interface!(my_WindowControlInterface, WindowControlInterface, {
    window_control_: Event<crate::WindowControl>
});

mw_com::import_type!(my_WindowInfo, crate::WindowInfo);
mw_com::import_type!(my_WindowControl, crate::WindowControl);
