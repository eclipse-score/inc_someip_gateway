// *******************************************************************************
// Copyright (c) 2026 Contributors to the Eclipse Foundation
//
// See the NOTICE file(s) distributed with this work for additional
// information regarding copyright ownership.
//
// This program and the accompanying materials are made available under the
// terms of the Apache License Version 2.0 which is available at
// https://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-FileCopyrightText: 2025, 2026 Contributors to the Eclipse Foundation
// SPDX-License-Identifier: Apache-2.0
// *******************************************************************************
use std::path::Path;
use std::thread::sleep;
use std::time::Duration;

use car_window_types::WindowCommand;
use car_window_types::WindowInfo;
use car_window_types::WindowState;

const MW_COM_CONFIG_PATH: &str = "examples/car_window_sim/config/mw_com_config.json";
const CONTROL_INSTANCE_SPECIFIER_ID: &str = "carwindow/WindowControl1";
const SERVICE_DISCOVERY_SLEEP_DURATION: Duration = Duration::from_millis(500);

fn update_state_machine(
    command: Option<car_window_types::WindowCommand>,
    winfo: &mut WindowInfo,
) -> bool {
    // window state is represented by position (0 = closed, 100 = open, in between = opening/closing/stopped).
    // the state represents the current action of the window.
    // the state open means the window is at position 100.
    // the state closed means the window is at position 0.
    // the state opening means the window is moving towards position 100.
    // the state closing means the window is moving towards position 0.
    // the state stopped means the window is not moving.
    // the command can be open, close, stop or None.
    // the command open means the state should be opening and the position should increase to 100 and then should stop automatically which means the state should be open.
    // the command close means the state should be closing and the position should decrease to 0 and then should stop automatically which means the state should be closed.
    // the command stop means the state should be stopped and the position should not automatically change.
    // the command None means no command is given and the state and position should automatically change according to the current state.
    let mut window_state = winfo.state;
    let mut position = winfo.pos;
    match command {
        Some(WindowCommand::Open) => {
            match window_state {
                WindowState::Open => {
                    // already open, do nothing
                }
                WindowState::Closed => {
                    window_state = WindowState::Opening;
                }
                WindowState::Opening => {
                    // already opening, do nothing
                }
                WindowState::Closing => {
                    window_state = WindowState::Stopped;
                }
                WindowState::Stopped => {
                    window_state = WindowState::Opening;
                }
            }
        }
        Some(WindowCommand::Close) => {
            match window_state {
                WindowState::Open => {
                    window_state = WindowState::Closing;
                }
                WindowState::Closed => {
                    // already closed, do nothing
                }
                WindowState::Opening => {
                    window_state = WindowState::Stopped;
                }
                WindowState::Closing => {
                    // already closing, do nothing
                }
                WindowState::Stopped => {
                    window_state = WindowState::Closing;
                }
            }
        }
        Some(WindowCommand::Stop) => {
            window_state = WindowState::Stopped;
        }
        None => {
            match window_state {
                WindowState::Open => {
                    // do nothing
                }
                WindowState::Closed => {
                    // do nothing
                }
                WindowState::Opening => {
                    if position < 100 {
                        position += 1;
                    } else {
                        window_state = WindowState::Open;
                    }
                }
                WindowState::Closing => {
                    if position > 0 {
                        position -= 1;
                    } else {
                        window_state = WindowState::Closed;
                    }
                }
                WindowState::Stopped => {
                    // do nothing
                }
            }
        }
    }
    // return true if state or position changed
    let changed = position != winfo.pos || window_state != winfo.state;
    winfo.state = window_state;
    winfo.pos = position;
    changed
}

fn main() {
    mw_com::initialize(Some(Path::new(MW_COM_CONFIG_PATH)));
    let control_instance_specifier =
        mw_com::InstanceSpecifier::try_from(CONTROL_INSTANCE_SPECIFIER_ID)
            .expect("Control Instance specifier creation failed");
    loop {
        let handles = loop {
            let handles = mw_com::proxy::find_service(control_instance_specifier.clone())
                .expect("Instance specifier resolution failed");
            if handles.len() > 0 {
                break handles;
            } else {
                println!("No control service found, retrying in 1 second");
                sleep(SERVICE_DISCOVERY_SLEEP_DURATION);
            }
        };

        let car_window_types::WindowControlInterface::Proxy { window_control_ } =
            car_window_types::WindowControlInterface::Proxy::new(&handles[0])
                .expect("Failed to create the proxy");
        let subscribed_window_control = window_control_.subscribe(1).expect("Failed to subscribe");
        println!("Subscribed!");
        //let mut command = None;
        let mut winfo = WindowInfo::default();
        winfo.state = WindowState::Closed;
        winfo.pos = 0;
        loop {
            let command = if let Some(y) = subscribed_window_control.get_new_sample() {
                println!("Got sample: {:#?}", y.command);
                Some(y.command)
            } else {
                None
            };
            if update_state_machine(command, &mut winfo) {
                println!("Window position: {}, state: {:?}", winfo.pos, winfo.state);
            }
            sleep(Duration::from_millis(20));
        }
    }
}
