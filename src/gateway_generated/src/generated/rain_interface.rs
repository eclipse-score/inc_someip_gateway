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

// Generated code example !!!!!!

use std::collections::HashMap;

use kyron::{select, spawn};
use com_api_gen::prelude::*;
use kyron_foundation::prelude::info;
use gateway_common::e2e::E2EProtectedType;
use gateway_common::e2e_no_protection::E2EShowcaseProfile;
use gateway_common::{E2EGatewayProfile, WriteBuf};
use gateway_common::{Payload, SomeIPEventDesc, SomeIPProducerNetworkHalf};

use crate::{EventMapping, FromSomeIP, SomeIPMappingTrait};
use crate::{
    LocalBridgeableSubscriptionTrait, PublisherSomeIPMappingTrait, SubscriberProxy, ToSomeIP,
};

impl FromSomeIP for RainSensor {
    type E2EGatewayProfile = E2EShowcaseProfile;

    fn from_someip(data: &Payload, _e2e: Option<&mut dyn E2EGatewayProfile>) -> Result<Self, ()> {
        info!("Will serialize {:?} as RainSensor", data);
        match data[0] {
            0 => return Ok(RainSensor { is_wet: false }),
            1 => return Ok(RainSensor { is_wet: true }),
            _ => panic!("Invalid bool in u8 {}", data[0]),
        }
    }
}

impl ToSomeIP for RainSensor {
    type E2EGatewayProfile = E2EShowcaseProfile;

    fn to_someip(
        &self,
        _output: &mut WriteBuf<'_>,
        _e2e: Option<&mut Self::E2EGatewayProfile>,
    ) -> Result<(), ()> {
        info!("Will serialize as RainSensor");
        Ok(())
    }
}

// Check if User and Gateway E2E Profile IDs are the same at compile time
const_assert_e2e_profile_id_eq!(RainSensor);

// This is generated code from tool. For mapping few options:
// - as of now
// - use str to hash and then in match hashes
// - any other ideas
impl SomeIPMappingTrait for RainOfferedProducer {
    fn get_event_mapping(name: &str) -> Option<EventMapping> {
        match name {
            "rain_sensor" => Some(EventMapping {
                mapping: core::mem::offset_of!(RainOfferedProducer, rain_sensor) as u64,
            }),
            _ => None,
        }
    }

    fn create_e2e_instance(mapping: EventMapping) -> impl E2EGatewayProfile {
        match mapping {
            EventMapping { mapping: m }
                if m == core::mem::offset_of!(RainOfferedProducer, rain_sensor) as u64 =>
            {
                <E2EProtectedType<RainSensor> as ToSomeIP>::E2EGatewayProfile::new()
            }
            _ => panic!("No E2E profile found for mapping: {}", mapping.mapping),
        }
    }
}

impl SomeIPMappingTrait for RainConsumer {
    fn get_event_mapping(name: &str) -> Option<EventMapping> {
        match name {
            "rain_sensor" => Some(EventMapping {
                mapping: core::mem::offset_of!(RainConsumer, rain_sensor) as u64,
            }),
            _ => None,
        }
    }

    fn create_e2e_instance(mapping: EventMapping) -> impl E2EGatewayProfile {
        match mapping {
            EventMapping { mapping: m }
                if m == core::mem::offset_of!(RainOfferedProducer, rain_sensor) as u64 =>
            {
                <E2EProtectedType<RainSensor> as ToSomeIP>::E2EGatewayProfile::new()
            }
            _ => panic!("No E2E profile found for mapping: {}", mapping.mapping),
        }
    }
}

impl PublisherSomeIPMappingTrait for RainOfferedProducer {
    fn get_publisher(&self, mapping: EventMapping) -> &dyn crate::SomeIPBridgeableEventTrait {
        match mapping.mapping {
            x if x == core::mem::offset_of!(RainOfferedProducer, rain_sensor) as u64 => {
                &self.rain_sensor
            }
            _ => panic!("No publisher found for mapping: {}", mapping.mapping),
        }
    }
}

impl LocalBridgeableSubscriptionTrait for RainConsumer {
    fn bridge<T>(
        self,
        events_info: &HashMap<EventMapping, SomeIPEventDesc>,
        someip_handle: T,
    ) -> kyron::JoinHandle<Result<(), com_api::prelude::Error>>
    where
        T: SomeIPProducerNetworkHalf + 'static,
    {
        // Generated code that will match mapping to actual Subscriber

        let rain_sensor_someip_desc = *events_info
            .get(&Self::get_event_mapping("rain_sensor").unwrap())
            .unwrap();

        let rain_sensor_proxy = SubscriberProxy::new(
            self.rain_sensor,
            1,
            someip_handle.clone(),
            rain_sensor_someip_desc,
        );

        spawn(async move {
            // using select! here may look strange but this is on of the options since `bridge_subscription` is never ends future
            // so this will basically poll all features all time (until error occurs)
            let res = select! {
                r1 = rain_sensor_proxy.bridge_subscription() => {
                    r1
                }
            };

            res
        })
    }
}
