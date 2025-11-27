/********************************************************************************
 * Copyright (c) 2025 ETAS GmbH
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
use anyhow::Result;
use std::io::{self};
use std::path::Path;

const MW_COM_CONFIG_PATH: &str = "examples/car_window_sim/config/mw_com_config.json";
const CONTROL_INSTANCE_SPECIFIER_ID: &str = "carwindow/WindowControl1";

// MAIN //
//#[tokio::main]
pub fn main() -> Result<()> {
    mw_com::initialize(Some(Path::new(MW_COM_CONFIG_PATH)));

    let control_instance_specifier =
        mw_com::InstanceSpecifier::try_from(CONTROL_INSTANCE_SPECIFIER_ID)
            .expect("Control Instance specifier creation failed");

    let skeleton =
        car_window_types::WindowControlInterface::Skeleton::new(&control_instance_specifier)
            .expect("Control Skeleton creation failed");

    let offered_skeleton: car_window_types::WindowControlInterface::Skeleton<
        mw_com::skeleton::Offered,
    > = skeleton
        .offer_service()
        .expect("Failed offering Control Skeleton");

    //let stdin = io::stdin();
    loop {
        let mut input = String::new();
        match io::stdin().read_line(&mut input) {
            Ok(_) => {
                // Process input
                println!("You entered: {}", input.trim_end());
                if input.trim_end() == "exit" {
                    println!("Exiting...");
                    break;
                }
                let cmd: car_window_types::WindowCommand =
                    match input.trim_end().to_lowercase().as_str() {
                        "open" => car_window_types::WindowCommand::Open,
                        "close" => car_window_types::WindowCommand::Close,
                        "stop" => car_window_types::WindowCommand::Stop,
                        _ => {
                            println!("Unknown command. Please enter 'open', 'close', or 'stop'.");
                            continue;
                        }
                    };

                let mut wincontrol: car_window_types::WindowControl =
                    car_window_types::WindowControl::default();
                wincontrol.command = cmd;
                offered_skeleton
                    .events
                    .window_control_
                    .send(wincontrol)
                    .expect(format!("Failed sending event: {:#?}", cmd).as_str());
            }
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                std::thread::sleep(std::time::Duration::from_millis(100));
                continue;
            }
            Err(e) => {
                eprintln!("Error reading input: {}", e);
                break;
            }
        }
    }
    offered_skeleton.stop_offer_service();
    Ok(())
}
