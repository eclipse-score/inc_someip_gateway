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
use kyron::JoinHandle;

pub mod bridges;

pub struct OutgoingServiceRunner {
    spawners: Vec<Box<dyn FnOnce() -> JoinHandle<Result<(), com_api::prelude::Error>> + Send>>,
}

impl OutgoingServiceRunner {
    pub fn new() -> Self {
        Self {
            spawners: Vec::new(),
        }
    }

    pub fn insert_spawner<F>(&mut self, spawner: F)
    where
        F: FnOnce() -> JoinHandle<Result<(), com_api::prelude::Error>> + Send + 'static,
    {
        self.spawners.push(Box::new(spawner));
    }

    pub async fn run_all(self) -> Result<(), com_api::prelude::Error> {
        let mut handles = Vec::new();
        for spawner in self.spawners {
            handles.push(spawner());
        }

        for handle in handles {
            handle.await.unwrap()?;
        }

        Ok(())
    }
}
