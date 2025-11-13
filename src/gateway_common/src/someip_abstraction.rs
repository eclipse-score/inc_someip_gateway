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

use com_api::prelude::InstanceSpecifier;

pub type EventId = u16;
pub type Payload = [u8];

pub type WriteBuf<'a> = kyron::io::ReadBuf<'a>;
/// Trait to write SOME/IP payload from ABI Compatible data Type. This allows
/// SOME/IP adapter to implement the write
pub trait SomeIPPayloadWriter {
    /// Provide buffer to write payload into it.
    ///
    /// # Attention
    /// User needs to call `advance_filled` when serialization is done to inform the writer how much was written into buffer
    ///
    /// # Consideration
    /// - The buffer shall be large enough to hold the payload for given deployment
    /// - The writer by intent does not provide any way to serialize data into buffer. This is because the writer does not have knowledge of the data structure being serialized
    ///   nor the serialization format. This enables to hide serialization details from Adapter and also to provide zero-copy serialization if needed as adapter can expose the
    ///   buffer that will be send directly to network.
    fn get_write_buf(&mut self) -> WriteBuf<'_>;

    /// Inform writer how much data was written into buffer provided by `get_write_buf`
    fn set_filled(&mut self, filled: usize);
}

/// SomeIP interactor trait to handle communication with SomeIP adapter
pub trait SomeIPConsumerHostHalf {
    /// Called when an event is received for given `event_id` with
    fn receive_event(&mut self, event_id: EventId, data: &Payload);

    // When requested service becomes available
    fn service_state_changed(&mut self, available: bool);

    fn get_service_description(&self) -> &ServiceDescription;

    fn get_event_interests(&self) -> &[SomeIPEventDesc];
}

// For SOME/IP Producer we split interface for a two halves:
// - HostHalf - this is interface that local service can use to interact with SOME/IP adapter
// - NetworkHalf - this is interface that SOME/IP adapter can use to interact with local service
//

/// This is where Producer can interact with SOME/IP adapter
pub trait SomeIPProducerNetworkHalf: Send + Sync + Clone {
    type EventWriterType: SomeIPPayloadWriter;

    /// Get writer for given event_id
    fn get_event_writer(&self, event_id: EventId) -> Self::EventWriterType;

    /// Called when local service shall be offered to SOME/IP network
    fn offer_service(&self, desc: OfferedServiceDesc) -> com_api::prelude::Result<()>;

    /// Called when there is new event that shall be published to SOME/IP network
    fn notify_event(&self, data: Self::EventWriterType) -> com_api::prelude::Result<()>;

    /// Register host half that will be used to deliver events from SOME/IP network to local service
    fn register_host_half(&self, half: Arc<dyn SomeIPProducerHostHalf>);

    fn get_service_description(&self) -> &ServiceDescription;
}

// This is where data from SOME/IP network is delivered to Producer on Host side (ie. req-resp, broken offer etc, for now not done)
pub trait SomeIPProducerHostHalf: Send + Sync {}

#[derive(Clone)]
pub struct ServiceDescription {
    pub service_id: u16,
    pub instance_id: u16,
    pub specifier: InstanceSpecifier,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EventType {
    Field,
    Event,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct SomeIPEventDesc {
    pub event_id: EventId,
    pub event_group_id: u16,
    pub typ: EventType,
}

pub struct OfferedServiceDesc {
    pub events: Vec<SomeIPEventDesc>,
}
