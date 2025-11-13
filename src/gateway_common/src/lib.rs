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
mod someip_abstraction;
pub use someip_abstraction::*;

pub mod e2e;
pub mod e2e_no_protection;

pub enum E2EGatewayError {
    CrcError,
    SequenceError,
}

/// Trait that describes how SOME/IP adapter should be implemented
pub trait SomeIPAdapter {
    type SomeIPHandleType;

    /// Obtain a handle to the SOME/IP producer for the given service description.
    fn obtain_producer_handle(&self, desc: ServiceDescription) -> Self::SomeIPHandleType;

    /// Register a service client that will handle incoming SOME/IP events.
    fn register_service_client<T: SomeIPConsumerHostHalf + Send + 'static>(
        &self,
        client: T,
    ) -> Result<(), com_api::prelude::Error>;

    /// Start the SOME/IP adapter, returning a future that runs the adapter's event loop.
    fn start(&self) -> impl std::future::Future<Output = ()> + Send;
}

pub trait E2EGatewayProfile: Send + 'static {
    /// Creates new profile instance.
    fn new() -> Self
    where
        Self: Sized;

    /// Check the E2E protection data in given payload. On success, the data payload refernce is returned, of failure correspoding error
    #[must_use = "E2E errors shall be checked"]
    fn check<'a>(
        &mut self,
        payload: &'a Payload,
    ) -> Result<(&'a Payload, u32), (E2EGatewayError, u32)>;

    /// Calculate E2E protection data for given `data` that shall be sent over SOME/IP
    fn calculate_e2e(&self, data: &Payload) -> Option<u32>;

    /// Returns the offset at which `data payload` start in E2E protected payload by this profile.
    /// This may be sometimes needed to access only data part ignoring failed E2E check.
    fn e2e_offset(&self) -> u32;

    /// Returns size of E2E protection data in bytes.
    fn e2e_size(&self) -> u8;

    fn profile_id(&self) -> u8;
}
