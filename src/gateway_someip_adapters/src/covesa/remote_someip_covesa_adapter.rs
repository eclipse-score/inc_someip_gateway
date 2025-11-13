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
#![allow(dead_code)]
#![allow(private_interfaces)]

use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
    thread,
    time::Duration,
};

use kyron::{futures::sleep, io::ReadBuf};
use kyron_foundation::prelude::{info, warn};

use gateway_common::{
    EventId, OfferedServiceDesc, ServiceDescription, SomeIPConsumerHostHalf, SomeIPPayloadWriter,
    SomeIPProducerHostHalf, SomeIPProducerNetworkHalf,
};

use iceoryx2::{
    node::NodeBuilder, prelude::ZeroCopySend, sample::Sample, sample_mut::SampleMut,
    sample_mut_uninit::SampleMutUninit, service::ipc_threadsafe,
};

#[derive(Debug, ZeroCopySend)]
#[repr(C)]
#[type_name("TunnelMsgType")]
enum TunnelMsgType {
    OfferService = 0,
    FindService = 1,
    OfferServiceAck = 2,
    FindServiceAck = 3,
    Message = 4,
    Event = 5,
    Notvalid = 6,
}

impl Default for TunnelMsgType {
    fn default() -> Self {
        TunnelMsgType::Notvalid
    }
}

#[derive(Debug, ZeroCopySend)]
#[repr(C)]
#[type_name("EventType")]
enum EventType {
    Field,
    Event,
}

#[derive(Debug, ZeroCopySend)]
#[repr(C)]
#[type_name("SomeIPEventDesc")]
struct SomeIPEventDesc {
    pub event_id: u16,
    pub event_groups: [u16; 4],
    pub len: u8,
    pub typ: EventType,
}

impl Default for SomeIPEventDesc {
    fn default() -> Self {
        Self {
            event_id: 0,
            event_groups: [0; 4],
            len: 0,
            typ: EventType::Event,
        }
    }
}

#[derive(Debug, Default, ZeroCopySend)]
#[repr(C)]
#[type_name("ServiceDescEntry")]
struct ServiceDescEntry {
    event_infos: [SomeIPEventDesc; 10],
    len: u8,
    //We can add other infos if we need
}

type OfferServiceEntry = ServiceDescEntry;

#[derive(Debug, Default, ZeroCopySend)]
#[repr(C)]
#[type_name("SomeipTunnelHeader")]
struct SomeipTunnelHeader {
    typ: TunnelMsgType,

    // below fields are optional based on type, for simplicity i did not modeled that
    service_id: u16,
    instance_id: u16,
    method_id: u16,

    service_metadata: ServiceDescEntry, //only when typ == FindService

    is_active: bool,

    id: u64, // shall be rewritten for response always
}

#[derive(Debug, ZeroCopySend)]
#[repr(C)]
#[type_name("SomeipTunnelPayload")]
struct SomeipTunnelPayload {
    length: u16,
    payload: [u8; 1500],
}

#[derive(Clone)]
pub struct RemoteCovesaAdapter {
    inner: Arc<Inner>,
}

#[derive(Clone)]
pub struct RemoteCovesaAdapterHandle {
    inner: Arc<Inner>,
    desc: ServiceDescription,
}

impl RemoteCovesaAdapterHandle {
    pub fn new(inner: Arc<Inner>, desc: ServiceDescription) -> Self {
        Self { inner, desc }
    }

    pub fn get_service_description(&self) -> &ServiceDescription {
        &self.desc
    }
}

enum SampleState {
    Unused(
        SampleMutUninit<
            ipc_threadsafe::Service,
            core::mem::MaybeUninit<SomeipTunnelPayload>,
            SomeipTunnelHeader,
        >,
    ),
    Used(SampleMut<ipc_threadsafe::Service, SomeipTunnelPayload, SomeipTunnelHeader>),
    Empty,
}
pub struct EventWriterImpl {
    event_id: EventId,
    sample: SampleMut<ipc_threadsafe::Service, SomeipTunnelPayload, SomeipTunnelHeader>,
}

impl SomeIPPayloadWriter for EventWriterImpl {
    fn get_write_buf(&mut self) -> ReadBuf<'_> {
        ReadBuf::new(self.sample.payload_mut().payload.as_mut_slice())
    }

    fn set_filled(&mut self, filled: usize) {
        self.sample.payload_mut().length = filled as u16;
    }
}

impl SomeIPProducerNetworkHalf for RemoteCovesaAdapterHandle {
    type EventWriterType = EventWriterImpl;

    fn offer_service(&self, offer_desc: OfferedServiceDesc) -> com_api::prelude::Result<()> {
        let desc = &self.desc;

        let mut event_desc = ServiceDescEntry::default();

        let mut cnt = 0;
        for event in offer_desc.events.iter() {
            let mut groups = [0_u16; 4];
            groups[0] = event.event_group_id;

            event_desc.event_infos[cnt] = SomeIPEventDesc {
                event_id: event.event_id,
                event_groups: groups,
                len: 1,
                typ: match event.typ {
                    gateway_common::EventType::Event => EventType::Event,
                    gateway_common::EventType::Field => EventType::Field,
                },
            };

            cnt += 1;
        }

        event_desc.len = cnt as u8;

        let header = SomeipTunnelHeader {
            typ: TunnelMsgType::OfferService,
            service_id: desc.service_id,
            instance_id: desc.instance_id,
            method_id: 0,
            is_active: false,
            service_metadata: event_desc,
            id: ((desc.service_id as u64) << 16 | desc.instance_id as u64) as u64,
        };

        let payload = SomeipTunnelPayload {
            length: 0,
            payload: [0; 1500],
        };

        let mut sample = self.inner.to_stack.loan_uninit().unwrap();
        info!("Offered service: {:?}", header);
        *sample.user_header_mut() = header;
        sample.write_payload(payload).send().unwrap();

        Ok(())
    }

    fn notify_event(&self, mut data: Self::EventWriterType) -> com_api::prelude::Result<()> {
        let header = SomeipTunnelHeader {
            typ: TunnelMsgType::Event,
            service_id: self.desc.service_id,
            instance_id: self.desc.instance_id,
            method_id: data.event_id,
            is_active: true,
            service_metadata: ServiceDescEntry::default(),
            id: ((self.desc.service_id as u64) << 16 | self.desc.instance_id as u64) as u64,
        };

        *data.sample.user_header_mut() = header;
        data.sample.send().unwrap();

        Ok(())
    }

    fn register_host_half(&self, half: Arc<dyn SomeIPProducerHostHalf>) {
        self.inner
            .host_producers
            .lock()
            .unwrap()
            .insert((self.desc.service_id, self.desc.instance_id), half);
    }

    fn get_service_description(&self) -> &ServiceDescription {
        &self.desc
    }

    fn get_event_writer(&self, event_id: EventId) -> Self::EventWriterType {
        let sample = self.inner.to_stack.loan_uninit().unwrap();

        EventWriterImpl {
            event_id,
            sample: unsafe { sample.assume_init() },
        }
    }
}

struct Inner {
    service_clients: Mutex<HashMap<(u16, u16), Box<dyn SomeIPConsumerHostHalf + Send>>>,
    host_producers: Mutex<HashMap<(u16, u16), Arc<dyn SomeIPProducerHostHalf>>>,

    from_stack: iceoryx2::port::subscriber::Subscriber<
        ipc_threadsafe::Service,
        SomeipTunnelPayload,
        SomeipTunnelHeader,
    >,
    to_stack: iceoryx2::port::publisher::Publisher<
        ipc_threadsafe::Service,
        SomeipTunnelPayload,
        SomeipTunnelHeader,
    >,
}

impl RemoteCovesaAdapter {
    pub fn new() -> Self {
        let node = NodeBuilder::new()
            .create::<ipc_threadsafe::Service>()
            .unwrap();

        let notifier = loop {
            match node
                .service_builder(&"LifetimeFromGateway".try_into().unwrap())
                .event()
                .event_id_max_value(0xFFFFFFFFFFFF)
                .open()
            {
                Ok(s) => break s.notifier_builder().create().unwrap(),
                Err(_) => {
                    info!("Waiting for gateway startup...");
                    std::thread::sleep(Duration::from_millis(20));
                    continue;
                }
            }
        };

        let listener = loop {
            match node
                .service_builder(&"LifetimeToGateway".try_into().unwrap())
                .event()
                .event_id_max_value(0xFFFFFFFFFFFF)
                .open()
            {
                Ok(s) => break s.listener_builder().create().unwrap(),
                Err(_) => {
                    info!("Waiting for gateway startup...");
                    std::thread::sleep(Duration::from_millis(20));
                    continue;
                }
            }
        };

        // We are ready
        notifier
            .notify_with_custom_event_id(iceoryx2::prelude::EventId::new(
                std::process::id() as usize
            ))
            .unwrap();

        listener.blocking_wait_one().unwrap(); // wait untill gateway is ready
        info!("Gateway ready, starting RemoteCovesaAdapter");

        let from_gateway = node
            .service_builder(&"TunnelFromRust".try_into().unwrap())
            .publish_subscribe::<SomeipTunnelPayload>()
            .subscriber_max_buffer_size(20)
            .history_size(20)
            .user_header::<SomeipTunnelHeader>()
            .open()
            .unwrap();

        let to_gateway = node
            .service_builder(&"TunnelToRust".try_into().unwrap())
            .publish_subscribe::<SomeipTunnelPayload>()
            .subscriber_max_buffer_size(20)
            .history_size(20)
            .user_header::<SomeipTunnelHeader>()
            .open()
            .unwrap();

        Self {
            inner: Arc::new(Inner {
                service_clients: Mutex::new(HashMap::new()),
                host_producers: Mutex::new(HashMap::new()),
                from_stack: to_gateway.subscriber_builder().create().unwrap(),
                to_stack: from_gateway.publisher_builder().create().unwrap(),
            }),
        }
    }

    pub async fn receive(
        &self,
    ) -> Sample<ipc_threadsafe::Service, SomeipTunnelPayload, SomeipTunnelHeader> {
        loop {
            match self.inner.from_stack.receive() {
                Ok(Some(sample)) => {
                    return sample;
                }
                Ok(None) => {
                    sleep::sleep(std::time::Duration::from_millis(50)).await;
                    continue;
                }
                Err(e) => {
                    warn!("Error receiving from SomeIP stack: {:?}", e);
                }
            }
        }
    }

    pub fn request_find_services(&self) {
        // Here we need to send all info for request_service

        for service in self.inner.service_clients.lock().unwrap().values() {
            let desc = service.get_service_description();

            let mut event_desc = ServiceDescEntry::default();

            let mut cnt = 0;
            for event in service.get_event_interests() {
                let mut groups = [0_u16; 4];
                groups[0] = event.event_group_id;

                event_desc.event_infos[cnt] = SomeIPEventDesc {
                    event_id: event.event_id,
                    event_groups: groups,
                    len: 1,
                    typ: match event.typ {
                        gateway_common::EventType::Event => EventType::Event,
                        gateway_common::EventType::Field => EventType::Field,
                    },
                };

                cnt += 1;
            }

            event_desc.len = cnt as u8;

            let header = SomeipTunnelHeader {
                typ: TunnelMsgType::FindService,
                service_id: desc.service_id,
                instance_id: desc.instance_id,
                method_id: 0,
                is_active: false,
                service_metadata: event_desc,
                id: ((desc.service_id as u64) << 16 | desc.instance_id as u64) as u64,
            };

            let payload = SomeipTunnelPayload {
                length: 0,
                payload: [0; 1500],
            };

            let mut sample = self.inner.to_stack.loan_uninit().unwrap();
            *sample.user_header_mut() = header;
            sample.write_payload(payload).send().unwrap();
            // TODO: Figure out why we need to wait here.
            // Tunnel seems to have problem when we send too fast
            thread::sleep(Duration::from_millis(500));
        }
    }
}

impl gateway_common::SomeIPAdapter for RemoteCovesaAdapter {
    type SomeIPHandleType = RemoteCovesaAdapterHandle;

    fn obtain_producer_handle(&self, desc: ServiceDescription) -> RemoteCovesaAdapterHandle {
        RemoteCovesaAdapterHandle::new(self.inner.clone(), desc)
    }

    fn register_service_client<T: SomeIPConsumerHostHalf + Send + 'static>(
        &self,
        client: T,
    ) -> Result<(), com_api::prelude::Error> {
        let desc = client.get_service_description();
        let service_id = desc.service_id;
        let instance_id = desc.instance_id;
        self.inner
            .service_clients
            .lock()
            .unwrap()
            .insert((service_id, instance_id), Box::new(client));

        Ok(())
    }

    // Starts bridging SOME/IP -> LOCAL
    async fn start(&self) {
        self.request_find_services();

        loop {
            let sample = self.receive().await;
            let header = sample.user_header();

            // The impl of this adapter is ad hock with locking etc. Fow current state it does not matter much

            //TODO: Here we have direct binding now for clients which forces the lock. But we could DO this instead:
            // - at start spawn clients as separate tasks with connected SPSC channel
            // - when something comes here, we can send via channel to proper client task
            // This way we can paralyze from here from 1 to N. The only problem is that
            // we need to sen Sample<> via channel which is possible for iceoryx and not for mw_com
            // Things to consider here later
            match header.typ {
                TunnelMsgType::FindServiceAck => {
                    if let Some(client) = self
                        .inner
                        .service_clients
                        .lock()
                        .unwrap()
                        .get_mut(&(header.service_id, header.instance_id))
                    {
                        info!("FindServiceAckWith: {:?}", header);

                        client.service_state_changed(header.is_active);
                    } else {
                        warn!(
                            "No client registered for service_id: {}, instance_id: {}",
                            header.service_id, header.instance_id
                        );
                    }
                }

                TunnelMsgType::Message => {
                    // Use common pattern to split method to lower half and event to upper.
                    match header.method_id {
                        0..0x8000 => {}
                        0x8000.. => {
                            if let Some(client) = self
                                .inner
                                .service_clients
                                .lock()
                                .unwrap()
                                .get_mut(&(header.service_id, header.instance_id))
                            {
                                let p = sample.payload();

                                client.receive_event(
                                    header.method_id,
                                    p.payload[0..p.length as usize].as_ref(),
                                );
                            } else {
                                warn!(
                                    "No client registered for service_id: {}, instance_id: {}",
                                    header.service_id, header.instance_id
                                );
                            }
                        }
                    }
                }

                _ => {
                    warn!("Received unsupported TunnelMsgType: {:?}", header.typ);
                }
            }
        }
    }
}
