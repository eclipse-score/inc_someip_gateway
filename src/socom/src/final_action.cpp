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

#include "final_action.hpp"

namespace score {
namespace socom {

Final_action::Final_action(F f) noexcept : m_f{std::move(f)} {}

Final_action::Final_action(Final_action&& other) noexcept : m_f{std::move(other.m_f)} {
    // it is not defined by C++ if a std::function after a move is reset or not. Reset it always to
    // get consistent behavior.
    other.m_f = nullptr;
}

Final_action::~Final_action() noexcept { execute(); }

void Final_action::execute() noexcept {
    try {
        if (m_f) {
            m_f();
        }
        m_f = nullptr;
    } catch (...) {
    }
}

}  // namespace socom
}  // namespace score
