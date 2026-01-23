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

#ifndef SCORE_SOCOM_FINAL_ACTION_HPP
#define SCORE_SOCOM_FINAL_ACTION_HPP

#include <functional>

namespace score {
namespace socom {

///
/// \class Final_action
///
/// \brief Wraps a functor that shall be executed only when an instance of this class gets destroyed
///
class Final_action {
   public:
    using F = std::function<void()>;

    ///
    /// \brief Constructor
    /// \param f functor to be called
    ///
    explicit Final_action(F f) noexcept;

    ///
    /// \brief Move constructor
    ///
    Final_action(Final_action&& other) noexcept;

    Final_action(Final_action const&) = delete;
    Final_action& operator=(Final_action const&) = delete;
    Final_action& operator=(Final_action&&) = delete;

    ///
    /// \brief Destructor
    ///
    ~Final_action() noexcept;

    ///
    /// \brief Runs the functor and disarms the Final_action. It will destroy the stored functor.
    ///
    void execute() noexcept;

   private:
    F m_f;
};

}  // namespace socom
}  // namespace score

#endif  // SCORE_SOCOM_FINAL_ACTION_HPP
