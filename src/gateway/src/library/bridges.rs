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
use kyron::futures::sleep;
use kyron::spawn;
use kyron::JoinHandle;
use com_api::prelude::Builder;
use com_api::prelude::ConsumerDescriptor;
use com_api::prelude::OfferedProducer;
use com_api::prelude::ServiceDiscovery;
use kyron_foundation::prelude::vector_extension::VectorExtension;
use kyron_foundation::prelude::*;
use gateway_common::E2EGatewayProfile;
use gateway_common::EventId;
use gateway_common::OfferedServiceDesc;
use gateway_common::Payload;
use gateway_common::ServiceDescription;
use gateway_common::SomeIPConsumerHostHalf;
use gateway_common::SomeIPEventDesc;
use gateway_common::SomeIPProducerHostHalf;
use gateway_common::SomeIPProducerNetworkHalf;
use gateway_generated::LocalBridgeableSubscriptionTrait;
use gateway_generated::PublisherSomeIPMappingTrait;
use gateway_generated::{EventMapping, SomeIPMappingTrait};
use std::ops::DerefMut;
use std::{collections::HashMap, sync::Arc};

pub enum LocalProducerState<T: com_api::prelude::Producer> {
    Unoffered(T),                // Once there is no SOME/IP available service
    Offered(T::OfferedProducer), // Once there is SOME/IP available service
    None,
}

impl<T: com_api::prelude::Producer> core::fmt::Debug for LocalProducerState<T> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            LocalProducerState::Unoffered(_) => write!(f, "LocalProducerState::Unoffered"),
            LocalProducerState::Offered(_) => write!(f, "LocalProducerState::Offered"),
            LocalProducerState::None => write!(f, "LocalProducerState::None"),
        }
    }
}

// Bridge to handle traffic from SomeIP to local producer
pub struct SomeIPToLocalBridge<ProducerType: com_api::prelude::Producer> {
    desc: ServiceDescription,
    producer: LocalProducerState<ProducerType>,
    e2e_instances: HashMap<EventMapping, Box<dyn E2EGatewayProfile>>,
    event_mapping: HashMap<EventId, EventMapping>,
    events: Vec<SomeIPEventDesc>, // I keep them here for now, maybe useful later
}

impl<ProducerType: com_api::prelude::Producer> SomeIPToLocalBridge<ProducerType>
where
    ProducerType::OfferedProducer: SomeIPMappingTrait,
{
    fn new(
        desc: ServiceDescription,
        events: Vec<(SomeIPEventDesc, &str)>,
        unoffered: ProducerType,
    ) -> Result<Self, com_api::prelude::Error> {
        let mut event_mapping = HashMap::new();
        let mut e2e_instances = HashMap::new();
        let mut events_only = Vec::new_in_global(10);

        for event in events.iter() {
            let mapping = ProducerType::OfferedProducer::get_event_mapping(event.1).unwrap();
            event_mapping.insert(event.0.event_id, mapping);
            events_only.push(event.0);

            let e2e = Box::new(ProducerType::OfferedProducer::create_e2e_instance(mapping));
            let e2e_dyn: Box<dyn E2EGatewayProfile> = e2e;

            e2e_instances.insert(mapping, e2e_dyn);
        }

        Ok(Self {
            desc,
            producer: LocalProducerState::Unoffered(unoffered),
            event_mapping: event_mapping,
            events: events_only,
            e2e_instances,
        })
    }
}

impl<ProducerType> SomeIPConsumerHostHalf for SomeIPToLocalBridge<ProducerType>
where
    ProducerType: com_api::prelude::Producer,
    ProducerType::OfferedProducer:
        PublisherSomeIPMappingTrait + com_api::prelude::OfferedProducer<Producer = ProducerType>,
{
    fn receive_event(&mut self, event_id: EventId, data: &Payload) {
        match &self.producer {
            LocalProducerState::Unoffered(_) => {
                warn!(
                    "Producer is unoffered, cannot bridge incomming SOME
                event {:x}",
                    event_id
                );
            }
            LocalProducerState::Offered(offered) => {
                if let Some(mapping) = self.event_mapping.get(&event_id) {
                    let publisher = offered.get_publisher(*mapping);
                    let e2e_instance = self.e2e_instances.get_mut(mapping).unwrap();
                    let e2e_ref = e2e_instance.deref_mut();

                    publisher
                        .bridge_event(data, Some(e2e_ref))
                        .unwrap_or_else(|e| {
                            warn!("Failed to bridge SomeIP event: {:?}", e);
                        });
                } else {
                    warn!("No mapping found for event_id: {:x}", event_id);
                }
            }
            LocalProducerState::None => {
                warn!(
                    "Producer is none, cannot bridge incoming SOME/IP event {:x}",
                    event_id
                );
            }
        }
    }

    fn service_state_changed(&mut self, available: bool) {
        let prev = core::mem::replace(&mut self.producer, LocalProducerState::None);

        match prev {
            LocalProducerState::Unoffered(unoffered) if available => {
                self.producer = LocalProducerState::Offered(unoffered.offer().unwrap());
            }
            LocalProducerState::Offered(offered) if !available => {
                let unoffered = offered.unoffer();
                self.producer = LocalProducerState::Unoffered(unoffered);
            }
            _ => {
                warn!(
                    "Wrong attempt for state change in service_state_changed (previous state is {:?}, incoming availability is {})",
                    prev,
                    available
                );
            }
        }
    }

    fn get_service_description(&self) -> &ServiceDescription {
        &self.desc
    }

    fn get_event_interests(&self) -> &[SomeIPEventDesc] {
        &self.events
    }
}

pub struct LocalToSomeIPBridge<
    ConsumerType: com_api::prelude::Consumer + Send + 'static,
    SomeIPAdapterType: SomeIPProducerNetworkHalf + 'static,
> {
    local_transport: Arc<com_api::prelude::RuntimeImpl>,
    someip_handle: SomeIPAdapterType,
    events: HashMap<EventMapping, SomeIPEventDesc>,
    _marker: core::marker::PhantomData<ConsumerType>,
}

unsafe impl<
        ConsumerType: com_api::prelude::Consumer + Send + 'static,
        SomeIPAdapterType: SomeIPProducerNetworkHalf + 'static,
    > Sync for LocalToSomeIPBridge<ConsumerType, SomeIPAdapterType>
{
}

impl<
        ConsumerType: com_api::prelude::Consumer + Send,
        SomeIPAdapterType: SomeIPProducerNetworkHalf,
    > SomeIPProducerHostHalf for LocalToSomeIPBridge<ConsumerType, SomeIPAdapterType>
{
}

impl<ConsumerType, SomeIPAdapterType> LocalToSomeIPBridge<ConsumerType, SomeIPAdapterType>
where
    ConsumerType:
        com_api::prelude::Consumer + LocalBridgeableSubscriptionTrait + SomeIPMappingTrait + Send,
    SomeIPAdapterType: SomeIPProducerNetworkHalf + Send,
    ConsumerType::Interface: com_api::prelude::Interface + Send,
    com_api::prelude::SampleConsumerBuilder<ConsumerType::Interface>:
        com_api::prelude::Builder<ConsumerType>,
{
    //TODO: For now self is arc, we can hide it if needed

    pub fn start(self: Arc<Self>) -> JoinHandle<Result<(), com_api::prelude::Error>> {
        spawn(async move {
            // Make sure SOME/IP adapter can call us if needed (ie methods etc)
            self.someip_handle.register_host_half(self.clone());

            let specifier = self
                .someip_handle
                .get_service_description()
                .specifier
                .clone();

            // Find service
            let consumer = self.find_service(specifier).await?;

            // Now offer service to the world
            self.offer_service().unwrap();

            // Now start bridging events from local consumer to SOME/IP
            let handle = consumer.bridge(&self.events, self.someip_handle.clone());

            let _ = handle.await.unwrap();

            Ok(())
        })
    }

    fn offer_service(&self) -> com_api::prelude::Result<()> {
        let desc = OfferedServiceDesc {
            events: self.events.values().cloned().collect(),
        };

        self.someip_handle.offer_service(desc)
    }

    fn new(
        events: Vec<(SomeIPEventDesc, &str)>,
        local_transport: Arc<com_api::prelude::RuntimeImpl>,
        someip_handle: SomeIPAdapterType,
    ) -> Self {
        let mut event_mapping = HashMap::new();

        for event in events.iter() {
            let mapping = ConsumerType::get_event_mapping(event.1).unwrap();
            event_mapping.insert(mapping, event.0);
        }

        Self {
            local_transport,
            someip_handle,
            events: event_mapping,
            _marker: core::marker::PhantomData,
        }
    }

    async fn find_service(
        &self,
        specifier: com_api::prelude::InstanceSpecifier,
    ) -> Result<ConsumerType, com_api::prelude::Error> {
        // For now emulate async find_service
        loop {
            let finder = self
                .local_transport
                .find_service::<ConsumerType::Interface>(specifier.clone());

            let found_instances = finder.get_available_instances()?;
            let maybe_instance = found_instances
                .into_iter()
                .find(|instance| instance.get_instance_id() == 42);

            if let Some(instance) = maybe_instance {
                // Found the instance, create consumer
                return instance.build();
            } else {
                sleep::sleep(std::time::Duration::from_secs(300)).await;
            }
        }
    }
}

pub struct BridgingServiceBuilder {}

impl BridgingServiceBuilder {
    /// Creates SomeIP to local bridge for given `description` and `events`.
    /// This means this instance will be `local Producer` and on SOME/IP it will be a `client` so it will find of service.
    ///
    pub fn create_someip_to_local_bridge<ProducerType>(
        description: ServiceDescription,
        events: Vec<(SomeIPEventDesc, &str)>,
        local_transport: &Arc<com_api::prelude::RuntimeImpl>,
    ) -> Result<SomeIPToLocalBridge<ProducerType>, com_api::prelude::Error>
    where
        ProducerType: com_api::prelude::Producer,
        ProducerType::OfferedProducer: SomeIPMappingTrait,
        ProducerType::Interface: core::fmt::Debug,
        com_api::prelude::SampleProducerBuilder<ProducerType::Interface>:
            com_api::prelude::Builder<ProducerType>,
    {
        let unoffered = local_transport
            .producer_builder::<ProducerType::Interface>(description.specifier.clone())
            .build()?;
        SomeIPToLocalBridge::new(description, events, unoffered)
    }

    pub fn create_local_to_someip_bridge<ConsumerType, SomeIPAdapterType>(
        events: Vec<(SomeIPEventDesc, &str)>,
        local_transport: Arc<com_api::prelude::RuntimeImpl>,
        someip_handle: SomeIPAdapterType,
    ) -> Result<LocalToSomeIPBridge<ConsumerType, SomeIPAdapterType>, com_api::prelude::Error>
    where
        SomeIPAdapterType: SomeIPProducerNetworkHalf + Send,
        ConsumerType: com_api::prelude::Consumer
            + LocalBridgeableSubscriptionTrait
            + SomeIPMappingTrait
            + Send,
        ConsumerType::Interface: com_api::prelude::Interface + Send,
        com_api::prelude::SampleConsumerBuilder<ConsumerType::Interface>:
            com_api::prelude::Builder<ConsumerType>,
    {
        Ok(LocalToSomeIPBridge::new(
            events,
            local_transport,
            someip_handle,
        ))
    }
}
