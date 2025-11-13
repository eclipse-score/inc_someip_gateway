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
// TODO: This shall be moved to inc_mw_com repository and be globally visible to all users

use std::ops::Deref;

use com_api::prelude::Reloc;

/// The E2E error that is produced by gatway connected to this data
#[derive(Debug, Clone, Copy, PartialEq, Eq, Reloc)]
#[repr(C)]
pub enum E2EError {
    NoError,
    CrcError,
    SequenceError,
}

/// The wrapper type that provides safe access to E2E protected type `T` along with additional metadata provided by gatway
/// that is handling this data.
#[derive(Reloc)]
#[repr(C)]
pub struct E2EProtectedType<T> {
    data: Option<T>,         // User data
    raw_e2e: u32,            // Raw E2E value that was extracted by connected profile
    gateway_check: E2EError, // Status computed by gateway
    locally_produced: bool,  // true when produced locallyby `from_local` API
}

impl<T: E2ETypeConnector> E2ETypeConnector for E2EProtectedType<T> {
    type ConnectedE2EProfile = T::ConnectedE2EProfile;
}

impl<T: std::fmt::Debug> std::fmt::Debug for E2EProtectedType<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("E2EProtectedType")
            .field("data", &self.data)
            .field("raw_e2e", &self.raw_e2e)
            .field("gateway_check", &self.gateway_check)
            .field("locally_produced", &self.locally_produced)
            .finish()
    }
}

/// E2E profile description
/// There are only few profiles defined in AUTOSAR, so we can provide some standard implementations probably
pub trait E2EApplicationProfile {
    const PROFILE_ID: u8;

    /// Checks provided `raw_e2e` for E2E errors
    ///
    /// # Considerations
    /// - The other part of implementation must know how to extract correctly this `raw_e2e` from SOME/IP payload.
    ///   This is not a concern of user code here.
    fn check(&mut self, raw_e2e: u32) -> bool;

    fn profile_id(&self) -> u8 {
        Self::PROFILE_ID
    }
}

/// This connector trait helps to connect any data type `T` to certain E2E profile, making sure it's consistent across
/// code base.
pub trait E2ETypeConnector {
    type ConnectedE2EProfile: E2EApplicationProfile;
}

impl<T: E2ETypeConnector> E2EProtectedType<T> {
    /// Used to create local data that shall be sent over gateway
    /// TODO: This also need to take T::ConnectedE2EProfile to produce part of E2E that is application specfic to me merged with at gateway (ie. probably profile22 etc.)
    pub fn from_local(data: T) -> Self {
        Self {
            data: Some(data),
            raw_e2e: 0,
            gateway_check: E2EError::NoError,
            locally_produced: true,
        }
    }

    /// Used to create data received from the gateway
    pub fn from_gateway(data: Option<T>, raw_e2e: u32, gateway_check: E2EError) -> Self {
        Self {
            data,
            raw_e2e,
            gateway_check,
            locally_produced: false,
        }
    }

    /// Check E2E using provided instance of E2EProfile
    pub fn checked(
        &self,
        e2e_instance: &mut T::ConnectedE2EProfile,
    ) -> Result<E2ECheckedType<T>, E2EErrorLocal<T>> {
        match self.gateway_check {
            E2EError::NoError if self.locally_produced => Ok(E2ECheckedType { data: self }),
            E2EError::NoError => {
                if e2e_instance.check(self.raw_e2e) {
                    Ok(E2ECheckedType { data: self })
                } else {
                    Err(E2EErrorLocal::LocalError)
                }
            }
            E2EError::CrcError => Err(E2EErrorLocal::CrcError),
            E2EError::SequenceError => Err(E2EErrorLocal::SequenceError(
                self.raw_e2e,
                E2EUncheckedType { data: self },
            )),
        }
    }

    /// Check E2E using provided closure
    pub fn checked_with(
        &self,
        checker: impl FnOnce(u32) -> bool,
    ) -> Result<E2ECheckedType<T>, E2EErrorLocal<T>> {
        match self.gateway_check {
            E2EError::NoError if self.locally_produced => Ok(E2ECheckedType { data: self }),
            E2EError::NoError => {
                if checker(self.raw_e2e) {
                    Ok(E2ECheckedType { data: self })
                } else {
                    Err(E2EErrorLocal::LocalError)
                }
            }
            E2EError::CrcError => Err(E2EErrorLocal::CrcError),
            E2EError::SequenceError => Err(E2EErrorLocal::SequenceError(
                self.raw_e2e,
                E2EUncheckedType { data: self },
            )),
        }
    }
}

/// The accessor to the user data `T` once E2E checking succeeded
pub struct E2ECheckedType<'a, T> {
    data: &'a E2EProtectedType<T>,
}

impl<T> Deref for E2ECheckedType<'_, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.data.data.as_ref().unwrap()
    }
}

/// The accessor to the user data `T` once E2E checking failed
#[derive(Debug)]
pub struct E2EUncheckedType<'a, T> {
    data: &'a E2EProtectedType<T>,
}

/// Errors that can be induced by E2E when checked at application side
#[derive(Debug)]
pub enum E2EErrorLocal<'a, T> {
    LocalError, // Used when local (user provided) E2E check fails
    CrcError,   // Used when gateway detected CRC error
    SequenceError(u32, E2EUncheckedType<'a, T>), // Used when gateway detected sequence error, provides raw E2E and data.
}

pub struct E2ESomeUserProfile {}

impl E2EApplicationProfile for E2ESomeUserProfile {
    const PROFILE_ID: u8 = 0;

    fn check(&mut self, _raw_e2e: u32) -> bool {
        true
    }
}

pub struct E2ESomeUserProfile2 {}

impl E2EApplicationProfile for E2ESomeUserProfile2 {
    const PROFILE_ID: u8 = 10;

    fn check(&mut self, _raw_e2e: u32) -> bool {
        true
    }
}
