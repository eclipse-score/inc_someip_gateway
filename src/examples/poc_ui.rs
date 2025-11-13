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
use com_api::prelude::*;
use com_api_gen::prelude::*;
use std::{thread, time};

fn main() {
    let runtime_builder = RuntimeBuilderImpl::new();
    let runtime = runtime_builder.build().unwrap();

    let consumer_discovery = runtime.find_service::<WindowsInterface>(InstanceSpecifier {
        specifier: "WindowsPosition".to_string(),
    });
    let available_service_instances = consumer_discovery.get_available_instances().unwrap();

    // Create consumer from first discovered service
    let consumer_builder = available_service_instances
        .into_iter()
        .find(|desc| desc.get_instance_id() == 42)
        .unwrap();
    let consumer = consumer_builder.build().unwrap();

    // Subscribe to one event
    let subscribed = consumer.windows_position.subscribe(1).unwrap();

    // Create sample buffer to be used during receive
    let mut sample_buf = SampleContainer::new();

    loop {
        match subscribed.try_receive(&mut sample_buf, 1) {
            Ok(0) => {
                // println!("No sample received");
                thread::sleep(time::Duration::from_millis(50));
            }
            Ok(_x) => {
                let received_sample = sample_buf.pop_front().unwrap();
                println!(
                    "UI: Windows position: FL: {}, FR: {}, RL: {}, RR: {}",
                    received_sample.fl, received_sample.fr, received_sample.rl, received_sample.rr
                );
            }
            Err(e) => panic!("{:?}", e),
        }
    }
}
