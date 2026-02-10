/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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

#ifndef SOCOM_BRIDGE_T_HPP
#define SOCOM_BRIDGE_T_HPP

#include <atomic>
#include <optional>

#include "connector_factory.hpp"
#include "socom_mocks.hpp"

namespace ac {

/// \brief Facade for simple bridge behaviour and callbacks in tests
///
/// It allows easy configuration of mocks and blocks its destruction until all
/// expectations have been fulfilled.
class Bridge_data {
   public:
    /// \brief Expect which callbacks will be called and thus need to be configured
    enum Expect { nothing, subscribe_find_service, request_service_function, both };

   private:
    ::score::socom::Bridge_identity m_identity{::score::socom::Bridge_identity::make(*this)};
    score::socom::Subscribe_find_service_function_mock m_sfsf_mock;
    score::socom::Request_service_function_mock m_rsf_mock;

    ::score::socom::Find_result_change_callback m_find_result_callback;

    std::atomic<bool> m_subscribe_find_service_created{false};
    std::atomic<bool> m_subscribe_find_service_destroyed{true};
    std::atomic<bool> m_request_find_service_created{false};
    std::atomic<bool> m_request_find_service_destroyed{true};

    ::score::socom::Service_bridge_registration m_bridge_registration{nullptr};

    ::score::Result<::score::socom::Service_bridge_registration> register_at_runtime(
        Connector_factory& connector_factory);

   public:
    /// \brief Set order of bridge callback registration and callback configuration.
    ///
    /// When configuring callbacks it needs to be known if there are already clients registered to
    /// the runtime
    enum Creation_sequence { bridge_then_expect, expect_then_bridge };

    using Subscribe_find_service_function = ::score::socom::Subscribe_find_service_function;

    static Subscribe_find_service_function sfs_do_nothing();

    /// \brief Create new Bridge facade
    ///
    /// \param[in] sequence whether to register or configure callbacks first
    /// \param[in] expect which callbacks will be called
    /// \param[in] connector_factory the runtime facade to register to
    /// \param[in] ctor_callback will be called from constructor
    /// \param[in] sfs_callback will be called during create_value for subscribe_find_service
    Bridge_data(
        Creation_sequence const& sequence, Expect const& expect,
        Connector_factory& connector_factory,
        std::function<void(Bridge_data&)>&& ctor_callback = [](Bridge_data& /*unused*/) {},
        Subscribe_find_service_function const& sfs_callback = sfs_do_nothing());

    Bridge_data(Bridge_data const&) = delete;
    Bridge_data(Bridge_data&&) = delete;

    ~Bridge_data();

    Bridge_data& operator=(Bridge_data const&) = delete;
    Bridge_data& operator=(Bridge_data&&) = delete;

    /// \brief Expect callbacks to be called
    ///
    /// \param[in] expect which callbacks will be called
    /// \param[in] connector_factory container of configuration and instance id
    /// \param[in] sfs_callback will be called during create_value for subscribe_find_service
    void expect_callbacks(Expect const& expect, Connector_factory const& connector_factory,
                          Subscribe_find_service_function const& sfs_callback = sfs_do_nothing());

    /// \brief Allow the Bridge_data instance to be destroyed before the expectations of the
    /// callbacks have been fulfilled
    void no_destroyed_check();

    /// \brief Report found service to the runtime
    ///
    /// \param[in] interface the interface to be reported
    /// \param[in] instance the instance to be reported
    /// \param[in] status the status of the reported service
    void find_service(::score::socom::Service_interface const& interface,
                      ::score::socom::Service_instance const& instance,
                      ::score::socom::Find_result_status const& status) const;

    /// \brief Expect call to subscribe_find_service()
    ///
    /// \param[in] interface
    /// \param[in] instance
    /// \param[in] sfs_callback will be called during create_value for subscribe_find_service
    void expect_subscribe_find_service(
        ::score::socom::Service_interface const& interface,
        std::optional<::score::socom::Service_instance> instance,
        Subscribe_find_service_function const& sfs_callback = sfs_do_nothing());

    /// \brief Expect call to subscribe_find_service(), callback only, for use with additional
    ///        requests not managed by Bridge_data
    ///
    /// \param[in] interface
    /// \param[in] instance
    /// \param[in] sfs_callback will be called during create_value for subscribe_find_service
    void expect_another_subscribe_find_service(
        ::score::socom::Service_interface const& interface,
        std::optional<::score::socom::Service_instance> instance,
        Subscribe_find_service_function const& sfs_callback = sfs_do_nothing());

    /// \brief Expect call to request_find_service()
    ///
    /// \param[in] configuration
    /// \param[in] instance
    /// \param[in] rsf Function to call when configured request_find_service() is called
    void expect_request_find_service(
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        std::function<void(::score::socom::Service_interface_configuration const&,
                           ::score::socom::Service_instance const&)>&& rsf =
            [](auto const& /*configuration*/, auto const& /*instance*/) {});

    /// \return atomic to check if runtime called request_find_service
    std::atomic<bool> const& get_request_find_service_created() const;

    /// \return atomic to check if request_find_service handle was destroyed by the runtime
    std::atomic<bool> const& get_request_find_service_destroyed() const;

    /// \return atomic to check if runtime called subscribe_find_service
    std::atomic<bool> const& get_subscribe_find_service_created() const;

    /// \return atomic to check if subscribe_find_service handle was destroyed by the runtime
    std::atomic<bool> const& get_subscribe_find_service_destroyed() const;

    std::optional<::score::socom::Bridge_identity> get_identity() const;
};

}  // namespace ac

#endif
