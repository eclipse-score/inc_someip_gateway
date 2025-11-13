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
use crate::{E2EGatewayError, E2EGatewayProfile, Payload};

/// Implements ['E2EGatewayProfile'] so the types that are send without E2E can use this as profile
pub struct NoE2EProtection {}

impl E2EGatewayProfile for NoE2EProtection {
    fn new() -> Self {
        Self {}
    }

    fn check<'a>(
        &mut self,
        payload: &'a Payload,
    ) -> Result<(&'a Payload, u32), (E2EGatewayError, u32)> {
        Ok((payload, 0))
    }

    fn e2e_offset(&self) -> u32 {
        0
    }

    fn e2e_size(&self) -> u8 {
        0
    }

    fn calculate_e2e(&self, _data: &Payload) -> Option<u32> {
        None
    }

    fn profile_id(&self) -> u8 {
        0
    }
}

// Implementes example E2E profile that is used to showcase usage
pub struct E2EShowcaseProfile {}

impl E2EGatewayProfile for E2EShowcaseProfile {
    fn new() -> Self {
        Self {}
    }

    fn check<'a>(
        &mut self,
        payload: &'a Payload,
    ) -> Result<(&'a Payload, u32), (E2EGatewayError, u32)> {
        let raw_e2e = payload[0];
        println!("Checking E2E for payload: {:?}", payload);

        if payload.len() < 2 {
            return Err((E2EGatewayError::CrcError, raw_e2e as u32));
        }
        let expected = payload[1] % 45;
        if raw_e2e != expected {
            return Err((E2EGatewayError::CrcError, raw_e2e as u32));
        }

        Ok((&payload[1..], raw_e2e as u32))
    }

    fn calculate_e2e(&self, _data: &Payload) -> Option<u32> {
        let bytes = _data as *const Payload as *const u8;

        let val = unsafe { bytes.read() };

        Some((val % 45) as u32) // Dummy implementation just to have sth
    }

    fn e2e_offset(&self) -> u32 {
        0
    }

    fn e2e_size(&self) -> u8 {
        1
    }

    fn profile_id(&self) -> u8 {
        0xA
    }
}
