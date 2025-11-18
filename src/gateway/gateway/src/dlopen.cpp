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

#include "dlopen.hpp"

#include <cassert>

#include <dlfcn.h>

namespace score::gateway {

namespace {
class Dlopen_impl : public Dlopen {
public:
    explicit Dlopen_impl(void* const handle) : handle_{handle} { assert(nullptr != handle_); }

    ~Dlopen_impl() override
    {
        if (handle_ != nullptr) {
            (void)::dlclose(handle_);
        }
    }

private:
    void* handle_{nullptr};
};
} // namespace

score::socom::Result<Dlopen::Uptr, Dlopen::Error> create_dlopen(std::string const& library_path)
{
    auto const handle = ::dlopen(library_path.c_str(), RTLD_NOW);

    if (nullptr == handle) {
        auto const error = ::dlerror();
        assert(error != nullptr);
        return score::socom::Result<Dlopen::Uptr, Dlopen::Error>::unexpected_type(error);
    }

    return score::socom::Result<Dlopen::Uptr, Dlopen::Error>{std::make_unique<Dlopen_impl>(handle)};
}


} // namespace score::gateway
