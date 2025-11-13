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
use std::collections::HashMap;

use kyron::futures::sleep;

use com_api::prelude::Reloc;
use com_api::prelude::SampleContainer;
use com_api::prelude::SampleMaybeUninit;
use com_api::prelude::SampleMut;
use com_api::prelude::Subscriber;
use com_api::prelude::Subscription;
use kyron_foundation::prelude::debug;
use kyron_foundation::prelude::error;
use kyron_foundation::prelude::warn;
use gateway_common::e2e::E2EProtectedType;
use gateway_common::e2e::E2ETypeConnector;
use gateway_common::E2EGatewayProfile;
use gateway_common::Payload;
use gateway_common::SomeIPEventDesc;
use gateway_common::SomeIPPayloadWriter;
use gateway_common::SomeIPProducerNetworkHalf;
use gateway_common::WriteBuf;

/// Internal idetifier that allows query a type that implements [`SomeIPMappingTrait`] for certain event
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EventMapping {
    mapping: u64,
}

pub trait SomeIPMappingTrait {
    /// Provides a mapping value based on `name` This can be fetched at startup and later used to query `get_publisher`.
    /// The idea is that once SOME/IP Event ID is fetched from config, together with EventName, this can be used to provide mapping.
    /// Returns `None` if no mapping is found.
    fn get_event_mapping(name: &str) -> Option<EventMapping>;

    /// Creates new E2E profile instance for given mapping.
    /// Panics if no mapping is found.
    fn create_e2e_instance(mapping: EventMapping) -> impl E2EGatewayProfile;
}

pub trait PublisherSomeIPMappingTrait: SomeIPMappingTrait {
    // TODO: Maybe we can improve this be using some VTable or sth to avoid dynamic dispatch? Nevertheless this is as C++ virtual function call so nothing "scarry" here
    fn get_publisher(&self, mapping: EventMapping) -> &dyn SomeIPBridgeableEventTrait;
}

pub trait ConsumerSomeIPMappingTrait: SomeIPMappingTrait {
    /// Fetches the subscriber based on `mapping`.
    /// Providing wrong mapping will cause `panic`.
    fn get_subscribers<T: Reloc + Send, SubType: Subscriber<T>>(
        &self,
        mapping: EventMapping,
    ) -> &SubType;
}

/// Trait to transform from SOME/IP payload to ABI Compatible Data Type
pub trait FromSomeIP: Sized + E2ETypeConnector {
    type E2EGatewayProfile: E2EGatewayProfile;

    fn from_someip(data: &Payload, e2e: Option<&mut dyn E2EGatewayProfile>) -> Result<Self, ()>;
}

// Trait to transform from ABI Compatible Data Type to SOME/IP
pub trait ToSomeIP: Sized + E2ETypeConnector {
    type E2EGatewayProfile: E2EGatewayProfile;

    fn to_someip(
        &self,
        output: &mut WriteBuf<'_>,
        e2e: Option<&mut Self::E2EGatewayProfile>,
    ) -> Result<(), ()>;
}

pub trait SomeIPBridgeableEventTrait {
    fn bridge_event(
        &self,
        data: &Payload,
        e2e: Option<&mut dyn E2EGatewayProfile>,
    ) -> com_api::prelude::Result<()>;
}

// Trait that shall be implemented by generated com_api::Consumer to provide bridge functionality for Subscriptions
// to SOME/IP. The implementation is inteded to be generated too.
pub trait LocalBridgeableSubscriptionTrait {
    fn bridge<T>(
        self,
        events_info: &HashMap<EventMapping, SomeIPEventDesc>,
        someip_handle: T,
    ) -> kyron::JoinHandle<Result<(), com_api::prelude::Error>>
    where
        T: SomeIPProducerNetworkHalf + 'static;
}

impl<T> SomeIPBridgeableEventTrait for com_api::prelude::Publisher<T>
where
    T: com_api::prelude::Reloc + core::fmt::Debug + FromSomeIP + Send,
{
    fn bridge_event(
        &self,
        data: &Payload,
        e2e: Option<&mut dyn E2EGatewayProfile>,
    ) -> com_api::prelude::Result<()> {
        let sample = T::from_someip(data, e2e).map_err(|_| com_api::prelude::Error::Fail)?;
        self.allocate().unwrap().write(sample).send()
    }
}

// Blanket impl for E2EProtectedType<T> to be used with FromSomeIP so when user models its type
// as E2EProtectedType<T> it will be automatically decoded from SOME/IP payload
impl<T> FromSomeIP for E2EProtectedType<T>
where
    T: Reloc + FromSomeIP,
{
    type E2EGatewayProfile = T::E2EGatewayProfile;

    fn from_someip(
        data: &Payload,
        e2e: Option<&mut dyn gateway_common::E2EGatewayProfile>,
    ) -> Result<Self, ()> {
        let checker = e2e.expect("E2E profile instance shall be provided for E2EProtectedType");

        match checker.check(&data) {
            Ok((payload, raw_e2e)) => Ok(E2EProtectedType::from_gateway(
                Some(T::from_someip(&payload, None)?),
                raw_e2e,
                gateway_common::e2e::E2EError::NoError,
            )),
            Err((gateway_common::E2EGatewayError::CrcError, raw_e2e)) => {
                // TODO: Shall there be configuration per type if it want's to still have send data even if CRC failed - maybe app layers still may do sth with it?
                error!(
                    "Failed to convert incoming SOME/IP payload {:?} using E2E profile {}. Data will not be serialized!",
                    data,
                    checker.profile_id()
                );
                Ok(E2EProtectedType::from_gateway(
                    None,
                    raw_e2e,
                    gateway_common::e2e::E2EError::CrcError,
                ))
            }
            Err((gateway_common::E2EGatewayError::SequenceError, _raw_e2e)) => todo!(), // As In E2E comment, this requires state, so question if this shall be done here. If yes then we need to store state and extend from_someip type
        }
    }
}

impl<T> ToSomeIP for E2EProtectedType<T>
where
    T: Reloc + ToSomeIP,
{
    type E2EGatewayProfile = T::E2EGatewayProfile;

    fn to_someip(
        &self,
        buffer: &mut WriteBuf<'_>,
        e2e: Option<&mut Self::E2EGatewayProfile>,
    ) -> Result<(), ()> {
        let e2e_instance = e2e.unwrap();
        let offset = e2e_instance.e2e_offset() as usize; // Where e2e data shall be written
        let size_of_e2e = e2e_instance.e2e_size() as usize;

        let mut filling_offset = 0;
        // Advance the output buffer to reserve space for E2E data
        if offset == 0 {
            unsafe { buffer.assume_init(size_of_e2e) };
            buffer.advance_filled(size_of_e2e);
            filling_offset = size_of_e2e;
        }

        // No need to do any check as data is produced locally, so it's correct.
        let data = self.checked_with(|_| true).map_err(|_| ())?;

        // Serialize data part
        data.to_someip(buffer, None)?;

        let serialized_data = &buffer.filled()[filling_offset..];

        e2e_instance
            .calculate_e2e(&serialized_data)
            .and_then(|e2e| {
                if offset != 0 {
                    unsafe { buffer.assume_init(size_of_e2e) };

                    let past_e2e_part = &mut buffer.filled_mut()[offset..];
                    let to_copy = past_e2e_part.len();

                    unsafe {
                        std::ptr::copy(
                            past_e2e_part.as_mut_ptr(),
                            past_e2e_part.as_mut_ptr().wrapping_add(size_of_e2e),
                            to_copy,
                        )
                    };

                    buffer.advance_filled(size_of_e2e);
                }

                // Write E2E data into reserved space
                unsafe {
                    buffer
                        .filled_mut()
                        .as_mut_ptr()
                        .wrapping_add(offset)
                        .cast::<u8>()
                        .copy_from(e2e as *const u8, size_of_e2e);
                }

                Some(e2e)
            });

        Ok(())
    }
}

/// Compile-time assert that T implements both X and Y
/// and that their associated consts PROFILE_ID are equal
#[macro_export]
macro_rules! const_assert_e2e_profile_id_eq {
    ($T:ty) => {
        // TODO: Check is dropped for a moment since there are dyn compat issues. However can be resolved later
        // const _: [(); 0 - !{
        //     <<$T as E2ETypeConnector>::ConnectedE2EProfile as E2EApplicationProfile>::PROFILE_ID
        //         == <$T as ToSomeIP>::E2EGatewayProfile::PROFILE_ID
        // } as usize] = [];
    };
}

/// Proxy struct that holds subscription to local com_api and SOME/IP handle to send events to SOME/IP network
pub struct SubscriberProxy<T, SubType, SubscriptionType, SomeIPHandle>
where
    T: core::fmt::Debug + com_api::prelude::Reloc + Send + ToSomeIP,
    SomeIPHandle: SomeIPProducerNetworkHalf + 'static,
    SubscriptionType: Subscription<T, Subscriber = SubType>,
{
    pub subscription: SubscriptionType,
    pub e2e_instance: T::E2EGatewayProfile,
    pub samples: usize,
    pub someip_handle: SomeIPHandle,
    pub desc: SomeIPEventDesc,

    phantom: core::marker::PhantomData<SubscriptionType>,
}

impl<T, SubType, SubscriptionType, SomeIPHandle>
    SubscriberProxy<T, SubType, SubscriptionType, SomeIPHandle>
where
    T: core::fmt::Debug + com_api::prelude::Reloc + Send + ToSomeIP + 'static,
    SomeIPHandle: SomeIPProducerNetworkHalf + 'static,
    SubscriptionType: Subscription<T, Subscriber = SubType> + Send + 'static,
    SubType: Subscriber<T, Subscription = SubscriptionType> + Send + 'static,
{
    fn new(
        subscriber: SubType,
        samples: usize,
        someip_handle: SomeIPHandle,
        desc: SomeIPEventDesc,
    ) -> Self {
        Self {
            subscription: subscriber.subscribe(samples).unwrap(),
            e2e_instance: T::E2EGatewayProfile::new(),
            samples,
            someip_handle,
            desc,
            phantom: core::marker::PhantomData,
        }
    }

    fn bridge_subscription(
        mut self,
    ) -> impl core::future::Future<Output = Result<(), com_api::prelude::Error>> {
        async move {
            let event_id = self.desc.event_id;
            let mut scratch = SampleContainer::new();

            loop {
                let res = self.subscription.receive(&mut scratch, 1, 1).await;
                match res {
                    Ok(_) => {}
                    Err(com_api::prelude::Error::Timeout) => {
                        // TODO: Remove sleep once the inc_mw_com binding will provide real async api implementation
                        sleep::sleep(core::time::Duration::from_millis(20)).await;
                        continue;
                    }
                    Err(e) => {
                        drop(scratch);
                        self.subscription.unsubscribe();
                        error!("Receiving sample ended with error: {:?}!", e);
                        return Err(e);
                    }
                };

                while let Some(s) = scratch.pop_front() {
                    debug!("Received sample for EventId {}: ", event_id);
                    let mut e_writer = self.someip_handle.get_event_writer(event_id);
                    let mut buffer = e_writer.get_write_buf();

                    if let Err(_) = s.to_someip(&mut buffer, Some(&mut self.e2e_instance)) {
                        warn!(
                        "Failed to convert received sample (for EventId {}) to SOME/IP, skipping!",
                        event_id
                    );
                        continue;
                    }

                    let filled = buffer.filled().len();
                    e_writer.set_filled(filled);

                    match self.someip_handle.notify_event(e_writer) {
                        Ok(_) => {}
                        Err(_) => {
                            warn!(
                        "Sending SOME/IP event (from sample: (currently missing Debug trait on sample)) failed for EventId {}",
                         event_id
                    );
                        }
                    }
                }
            }
        }
    }
}

// Import generated code
mod generated;
