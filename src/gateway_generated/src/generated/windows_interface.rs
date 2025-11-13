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
use gateway_common::e2e_no_protection::NoE2EProtection;
use gateway_common::{
    E2EGatewayProfile, Payload, SomeIPEventDesc, SomeIPProducerNetworkHalf, WriteBuf,
};

use crate::{EventMapping, FromSomeIP, SomeIPMappingTrait};
use crate::{
    LocalBridgeableSubscriptionTrait, PublisherSomeIPMappingTrait, SubscriberProxy, ToSomeIP,
};

impl FromSomeIP for WindowsPosition {
    type E2EGatewayProfile = NoE2EProtection;

    fn from_someip(
        data: &Payload,
        _e2e: Option<&mut dyn gateway_common::E2EGatewayProfile>,
    ) -> Result<Self, ()> {
        info!("Will serialize {:?} as WindowsPosition", data);
        Ok(WindowsPosition {
            fl: data[0],
            fr: data[1],
            rl: data[2],
            rr: data[3],
        })
    }
}

impl FromSomeIP for CloseWindows {
    type E2EGatewayProfile = NoE2EProtection;

    fn from_someip(
        data: &Payload,
        _e2e: Option<&mut dyn gateway_common::E2EGatewayProfile>,
    ) -> Result<Self, ()> {
        info!("Will serialize {:?} as CloseWindows", data);
        match data[0] {
            0 => return Ok(CloseWindows { close: false }),
            1 => return Ok(CloseWindows { close: true }),
            _ => panic!("Invalid bool in u8 {}", data[0]),
        }
    }
}

impl ToSomeIP for WindowsPosition {
    type E2EGatewayProfile = NoE2EProtection;

    fn to_someip(
        &self,
        _output: &mut WriteBuf<'_>,
        _e2e: Option<&mut Self::E2EGatewayProfile>,
    ) -> Result<(), ()> {
        info!("Will serialize as WindowsPosition");
        Ok(())
    }
}

impl ToSomeIP for CloseWindows {
    type E2EGatewayProfile = NoE2EProtection;

    fn to_someip(
        &self,
        output: &mut WriteBuf<'_>,
        _e2e: Option<&mut Self::E2EGatewayProfile>,
    ) -> Result<(), ()> {
        unsafe {
            core::ptr::copy_nonoverlapping(
                self as *const CloseWindows as *const u8,
                output.unfilled_mut().as_mut_ptr().cast::<u8>(),
                1,
            );

            output.assume_init(size_of::<CloseWindows>());
            output.set_filled(size_of::<CloseWindows>());
        };

        info!("Serialized CloseWindows: {}", size_of::<CloseWindows>());
        Ok(())
    }
}

// This is generated code from tool. For mapping few options:
// - as of now
// - use str to hash and then in match hashes
// - any other ideas?
impl SomeIPMappingTrait for WindowsOfferedProducer {
    fn get_event_mapping(name: &str) -> Option<EventMapping> {
        match name {
            "windows_position" => Some(EventMapping {
                mapping: core::mem::offset_of!(WindowsOfferedProducer, windows_position) as u64,
            }),
            "close_windows" => Some(EventMapping {
                mapping: core::mem::offset_of!(WindowsOfferedProducer, close_windows) as u64,
            }),
            _ => None,
        }
    }

    fn create_e2e_instance(mapping: EventMapping) -> impl E2EGatewayProfile {
        match mapping {
            EventMapping { mapping: m }
                if m == core::mem::offset_of!(WindowsOfferedProducer, windows_position) as u64 =>
            {
                <E2EProtectedType<WindowsPosition> as ToSomeIP>::E2EGatewayProfile::new()
            }
            EventMapping { mapping: m }
                if m == core::mem::offset_of!(WindowsOfferedProducer, close_windows) as u64 =>
            {
                <E2EProtectedType<CloseWindows> as ToSomeIP>::E2EGatewayProfile::new()
            }
            _ => panic!("No E2E profile found for mapping: {}", mapping.mapping),
        }
    }
}

impl SomeIPMappingTrait for WindowsConsumer {
    fn get_event_mapping(name: &str) -> Option<EventMapping> {
        match name {
            "windows_position" => Some(EventMapping {
                mapping: core::mem::offset_of!(WindowsConsumer, windows_position) as u64,
            }),
            "close_windows" => Some(EventMapping {
                mapping: core::mem::offset_of!(WindowsConsumer, close_windows) as u64,
            }),
            _ => None,
        }
    }

    fn create_e2e_instance(mapping: EventMapping) -> impl E2EGatewayProfile {
        match mapping {
            EventMapping { mapping: m }
                if m == core::mem::offset_of!(WindowsOfferedProducer, windows_position) as u64 =>
            {
                <E2EProtectedType<WindowsPosition> as ToSomeIP>::E2EGatewayProfile::new()
            }
            EventMapping { mapping: m }
                if m == core::mem::offset_of!(WindowsOfferedProducer, close_windows) as u64 =>
            {
                <E2EProtectedType<CloseWindows> as ToSomeIP>::E2EGatewayProfile::new()
            }
            _ => panic!("No E2E profile found for mapping: {}", mapping.mapping),
        }
    }
}

impl PublisherSomeIPMappingTrait for WindowsOfferedProducer {
    fn get_publisher(&self, mapping: EventMapping) -> &dyn crate::SomeIPBridgeableEventTrait {
        match mapping.mapping {
            x if x == core::mem::offset_of!(WindowsOfferedProducer, windows_position) as u64 => {
                &self.windows_position
            }
            x if x == core::mem::offset_of!(WindowsOfferedProducer, close_windows) as u64 => {
                &self.close_windows
            }
            _ => panic!("No publisher found for mapping: {}", mapping.mapping),
        }
    }
}

impl LocalBridgeableSubscriptionTrait for WindowsConsumer {
    fn bridge<T>(
        self,
        events_info: &HashMap<EventMapping, SomeIPEventDesc>,
        someip_handle: T,
    ) -> kyron::JoinHandle<Result<(), com_api::prelude::Error>>
    where
        T: SomeIPProducerNetworkHalf + 'static,
    {
        let close_windows_someip_desc = *events_info
            .get(&Self::get_event_mapping("close_windows").unwrap())
            .unwrap();

        let close_windows_proxy = SubscriberProxy::new(
            self.close_windows,
            1,
            someip_handle.clone(),
            close_windows_someip_desc,
        );

        spawn(async move {
            // so this will basically poll all features all time (until error occurs)
            select! {
                r1 = close_windows_proxy.bridge_subscription() => {
                    r1
                }
            }
        })
    }
}
