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
mod interface;

use com_api::prelude::*;
use com_api_runtime_iceoryx::{
    Publisher, RuntimeImpl, SampleConsumerBuilder, SampleProducerBuilder, SubscribableImpl,
};
use gateway_common::e2e::E2EProtectedType;
use iceoryx2::prelude::*;

pub use self::interface::*;

#[derive(Debug)]
#[repr(C)]
pub struct WindowsInterface {}

#[derive(Debug)]
#[repr(C)]
pub struct RainInterface {}

/// Generic
impl Interface for WindowsInterface {}
unsafe impl ZeroCopySend for WindowsInterface {}
impl Interface for RainInterface {}
unsafe impl ZeroCopySend for RainInterface {}

pub struct WindowsProducer {
    pub windows_position: Publisher<WindowsPosition>,
    pub close_windows: Publisher<CloseWindows>,
}

impl Producer for WindowsProducer {
    type Interface = WindowsInterface;
    type OfferedProducer = WindowsOfferedProducer;

    fn offer(self) -> com_api::prelude::Result<Self::OfferedProducer> {
        Ok(WindowsOfferedProducer {
            windows_position: self.windows_position,
            close_windows: self.close_windows,
        })
    }
}

pub struct RainProducer {
    pub rain_sensor: Publisher<E2EProtectedType<RainSensor>>,
}

impl Producer for RainProducer {
    type Interface = RainInterface;
    type OfferedProducer = RainOfferedProducer;

    fn offer(self) -> com_api::prelude::Result<Self::OfferedProducer> {
        Ok(RainOfferedProducer {
            rain_sensor: self.rain_sensor,
        })
    }
}

pub struct WindowsOfferedProducer {
    pub windows_position: Publisher<WindowsPosition>,
    pub close_windows: Publisher<CloseWindows>,
}

impl OfferedProducer for WindowsOfferedProducer {
    type Interface = WindowsInterface;
    type Producer = WindowsProducer;

    fn unoffer(self) -> Self::Producer {
        WindowsProducer {
            windows_position: self.windows_position,
            close_windows: self.close_windows,
        }
    }
}

pub struct RainOfferedProducer {
    pub rain_sensor: Publisher<E2EProtectedType<RainSensor>>,
}

impl OfferedProducer for RainOfferedProducer {
    type Interface = RainInterface;
    type Producer = RainProducer;

    fn unoffer(self) -> Self::Producer {
        RainProducer {
            rain_sensor: self.rain_sensor,
        }
    }
}

impl Builder<WindowsProducer> for SampleProducerBuilder<WindowsInterface> {
    fn build(self) -> com_api::prelude::Result<WindowsProducer> {
        let windows_position_service_name =
            format!("{}/windows_position", self.instance_specifier.specifier);
        let windows_position = Publisher::new(
            self.node
                .service_builder(&ServiceName::new(windows_position_service_name.as_str()).unwrap())
                .publish_subscribe::<WindowsPosition>()
                .open_or_create()
                .unwrap(),
        );
        let close_windows_service_name =
            format!("{}/close_windows", self.instance_specifier.specifier);
        let close_windows = Publisher::new(
            self.node
                .service_builder(&ServiceName::new(close_windows_service_name.as_str()).unwrap())
                .publish_subscribe::<CloseWindows>()
                .open_or_create()
                .unwrap(),
        );
        Ok(WindowsProducer {
            windows_position,
            close_windows,
        })
    }
}

impl ProducerBuilder<WindowsInterface, RuntimeImpl, WindowsProducer>
    for SampleProducerBuilder<WindowsInterface>
{
}

impl Builder<RainProducer> for SampleProducerBuilder<RainInterface> {
    fn build(self) -> com_api::prelude::Result<RainProducer> {
        let rain_sensor_service_name = format!("{}/rain_sensor", self.instance_specifier.specifier);
        let rain_sensor = Publisher::new(
            self.node
                .service_builder(&ServiceName::new(rain_sensor_service_name.as_str()).unwrap())
                .publish_subscribe::<E2EProtectedType<RainSensor>>()
                .open_or_create()
                .unwrap(),
        );
        Ok(RainProducer { rain_sensor })
    }
}

impl ProducerBuilder<RainInterface, RuntimeImpl, RainProducer>
    for SampleProducerBuilder<RainInterface>
{
}

pub struct WindowsConsumer {
    pub windows_position: SubscribableImpl<WindowsPosition>,
    pub close_windows: SubscribableImpl<CloseWindows>,
}

impl Consumer for WindowsConsumer {
    type Interface = WindowsInterface;
}

impl Builder<WindowsConsumer> for SampleConsumerBuilder<WindowsInterface> {
    fn build(self) -> com_api::prelude::Result<WindowsConsumer> {
        let windows_position_service_name =
            format!("{}/windows_position", self.instance_specifier.specifier);
        let windows_position = self
            .node
            .service_builder(&ServiceName::new(windows_position_service_name.as_str()).unwrap())
            .publish_subscribe::<WindowsPosition>()
            .open_or_create()
            .unwrap();
        let close_windows_service_name =
            format!("{}/close_windows", self.instance_specifier.specifier);
        let close_windows = self
            .node
            .service_builder(&ServiceName::new(close_windows_service_name.as_str()).unwrap())
            .publish_subscribe::<CloseWindows>()
            .open_or_create()
            .unwrap();
        Ok(WindowsConsumer {
            windows_position: SubscribableImpl::new(windows_position),
            close_windows: SubscribableImpl::new(close_windows),
        })
    }
}

pub struct RainConsumer {
    pub rain_sensor: SubscribableImpl<E2EProtectedType<RainSensor>>,
}

impl Consumer for RainConsumer {
    type Interface = RainInterface;
}

impl Builder<RainConsumer> for SampleConsumerBuilder<RainInterface> {
    fn build(self) -> com_api::prelude::Result<RainConsumer> {
        let rain_sensor_service_name = format!("{}/rain_sensor", self.instance_specifier.specifier);
        let rain_sensor = self
            .node
            .service_builder(&ServiceName::new(rain_sensor_service_name.as_str()).unwrap())
            .publish_subscribe::<E2EProtectedType<RainSensor>>()
            .open_or_create()
            .unwrap();
        Ok(RainConsumer {
            rain_sensor: SubscribableImpl::new(rain_sensor),
        })
    }
}
