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
use iceoryx2::prelude::*;

fn main() {
    set_log_level(iceoryx2::prelude::LogLevel::Debug);
    let runtime_builder = RuntimeBuilderImpl::new();
    let runtime = runtime_builder.build().unwrap();
    let producer_builder = runtime.producer_builder::<WindowsInterface>(InstanceSpecifier {
        specifier: "CloseWindows".to_string(),
    });
    let producer: WindowsProducer = producer_builder.build().unwrap();
    let offered_producer = producer.offer().unwrap();

    // Create service discovery
    let consumer_discovery = runtime.find_service::<RainInterface>(InstanceSpecifier {
        specifier: "RainSensor".to_string(),
    });
    let available_service_instances = consumer_discovery.get_available_instances().unwrap();

    // Create consumer from first discovered service
    let consumer_builder = available_service_instances
        .into_iter()
        .find(|desc| desc.get_instance_id() == 42)
        .unwrap();
    let consumer = consumer_builder.build().unwrap();

    // Subscribe to one event
    let subscribed = consumer.rain_sensor.subscribe(1).unwrap();

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

                // E2E check
                match received_sample.checked_with(|_| true) {
                    Ok(data) => {
                        println!("Received rain sensor status : {:?}", data.is_wet);
                        let uninit_sample = offered_producer.close_windows.allocate().unwrap();
                        let sample = uninit_sample.write(CloseWindows { close: data.is_wet });
                        sample.send().unwrap();
                        println!("Sent windows close status : {:?}", data.is_wet);
                    }
                    Err(e) => {
                        println!("Received rain sensor status with E2E error {:?}", e);
                        continue;
                    }
                }
            }
            Err(e) => panic!("{:?}", e),
        }
    }
}
