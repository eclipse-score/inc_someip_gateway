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
use std::sync::Arc;

use crate::library::bridges::BridgingServiceBuilder;
use crate::library::OutgoingServiceRunner;
use kyron::prelude::*;
use com_api::prelude::Builder;
use com_api_gen::prelude::{RainProducer, WindowsConsumer, WindowsProducer};
use kyron_foundation::prelude::{Vec, Vector};
use kyron_foundation::prelude::vector_extension::VectorExtension;
use kyron_foundation::prelude::{tracing_subscriber, Level};
use gateway_common::SomeIPAdapter;
use gateway_common::{EventType, ServiceDescription, SomeIPEventDesc};

mod library;

// This aliases can be later under `feature` flag for different adapters
type SomeIPAdapterType =
    gateway_someip_adapters::covesa::remote_someip_covesa_adapter::RemoteCovesaAdapter;

type SomeIPAdapterHandleType =
    gateway_someip_adapters::covesa::remote_someip_covesa_adapter::RemoteCovesaAdapterHandle;

fn main() {
    tracing_subscriber::fmt()
        // .with_span_events(FmtSpan::FULL) // Ensures span open/close events are logged
        .with_target(false) // Optional: Remove module path
        .with_max_level(Level::INFO)
        .with_thread_ids(true)
        .with_thread_names(true)
        .init();

    let (builder, _engine_id) = RuntimeBuilder::new().with_engine(
        ExecutionEngineBuilder::new()
            .task_queue_size(256)
            .workers(3),
    );
    let mut runtime = builder.build().unwrap();

    // set_log_level(iceoryx2::prelude::LogLevel::Debug);
    let local_transport = Arc::new(com_api::prelude::RuntimeBuilderImpl::new().build().unwrap());
    let adapter = SomeIPAdapterType::new();

    // Configure incoming services (SOME/IP -> LOCAL)
    configure_incoming_services(&adapter, &local_transport).unwrap();

    // Configure outgoing services (LOCAL - > SOME/IP)
    let spawner = configure_outgoing_services(&adapter, &local_transport).unwrap();

    runtime.block_on(async move {
        //Just run one task to bridge all traffic from SOME/IP  & handle external services into local services
        let incoming_handle = kyron::spawn(async move {
            adapter.start().await;
        });

        // Run all outgoing services (LOCAL -> SOME/IP)
        spawner.run_all().await.unwrap();
        incoming_handle.await.unwrap();
    });
}

// TODO: Later there is config loader needed now this acts as "config-by-code"
fn configure_incoming_services(
    adapter: &SomeIPAdapterType,
    local_transport: &Arc<com_api::prelude::RuntimeImpl>,
) -> Result<(), com_api::prelude::Error> {
    {
        let mut events_windows = Vec::new_in_global(1);

        events_windows.push((
            SomeIPEventDesc {
                event_id: 0x8003,
                event_group_id: 3,
                typ: EventType::Field,
            },
            "windows_position", // Event (so Publisher) field name
        ));

        let windows_bridge =
            BridgingServiceBuilder::create_someip_to_local_bridge::<WindowsProducer>(
                ServiceDescription {
                    service_id: 0x1000,
                    instance_id: 1,
                    specifier: com_api::prelude::InstanceSpecifier {
                        specifier: "WindowsPosition".to_string(),
                    },
                },
                events_windows,
                &local_transport,
            )?;

        adapter.register_service_client(windows_bridge)?;
    }

    {
        let mut events_rain = Vec::new_in_global(1);

        events_rain.push((
            SomeIPEventDesc {
                event_id: 0x8004,
                event_group_id: 4,
                typ: EventType::Field,
            },
            "rain_sensor", // Event (so Publisher) field name
        ));
        let rain_bridge = BridgingServiceBuilder::create_someip_to_local_bridge::<RainProducer>(
            ServiceDescription {
                service_id: 0x1001,
                instance_id: 1,
                specifier: com_api::prelude::InstanceSpecifier {
                    specifier: "RainSensor".to_string(),
                },
            },
            events_rain,
            &local_transport,
        )?;

        adapter.register_service_client(rain_bridge)?;
    }

    Ok(())
}

// TODO: Later there is config loader needed now this acts as "config-by-code"
fn configure_outgoing_services(
    adapter: &SomeIPAdapterType,
    local_transport: &Arc<com_api::prelude::RuntimeImpl>,
) -> Result<OutgoingServiceRunner, com_api::prelude::Error> {
    let mut spawner = library::OutgoingServiceRunner::new();
    // Example outgoing service configuration
    {
        let mut events = Vec::new_in_global(1);

        events.push((
            SomeIPEventDesc {
                event_id: 0x8015,
                event_group_id: 15,
                typ: EventType::Field,
            },
            "close_windows", // Event (so Publisher) field name
        ));

        let some_bridge = Arc::new(BridgingServiceBuilder::create_local_to_someip_bridge::<
            WindowsConsumer,
            SomeIPAdapterHandleType,
        >(
            events,
            local_transport.clone(),
            adapter.obtain_producer_handle(ServiceDescription {
                service_id: 0x1010,
                instance_id: 1,
                specifier: com_api::prelude::InstanceSpecifier {
                    specifier: "CloseWindows".to_string(),
                },
            }),
            // Missing SOME/IP transport still
        )?);

        spawner.insert_spawner(move || some_bridge.start());
    }

    Ok(spawner)
}
