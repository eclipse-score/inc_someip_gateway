/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef SCORE_MW_COM_IPC_BRIDGE_DLOPEN_H
#define SCORE_MW_COM_IPC_BRIDGE_DLOPEN_H

#include <score/socom/result.hpp>

#include <memory>
#include <string>

namespace score::gateway {

/// \brief RAII wrapper for a dynamically loaded shared library
///
/// It only loads the shared library into memory and does no further `dlsym` operations.
/// Plugin registration is done with static variables.
class Dlopen {
public:
    using Uptr = std::unique_ptr<Dlopen>;
    using Error = std::string;

    Dlopen() = default;

    virtual ~Dlopen() = default;

    Dlopen(Dlopen const&) = delete;
    Dlopen(Dlopen&&) = delete;

    Dlopen& operator=(Dlopen const&) = delete;
    Dlopen& operator=(Dlopen&&) = delete;
};

/// \brief The Dlopen instance constructor.
/// \param[in] library_path Path to the shared library.
score::socom::Result<Dlopen::Uptr, Dlopen::Error> create_dlopen(std::string const& library_path);

} // namespace score::gateway

#endif // SCORE_MW_COM_IPC_BRIDGE_SAMPLE_SENDER_RECEIVER_H
