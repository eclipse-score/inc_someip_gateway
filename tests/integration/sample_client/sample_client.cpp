/********************************************************************************
 * Copyright (c) 2025 ETAS GmbH
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

#include <iostream>
#include <memory>
#include <set>
#include <vsomeip/vsomeip.hpp>

#define SAMPLE_SERVICE_ID 0x1234
#define RESPONSE_SAMPLE_SERVICE_ID 0x4321
#define SAMPLE_INSTANCE_ID 0x5678
#define SAMPLE_EVENT_ID 0x8778
#define SAMPLE_EVENTGROUP_ID 0x4465

class SampleClient {
   public:
    SampleClient() : app_(vsomeip::runtime::get()->create_application("sample_client")) {}

    void on_state(vsomeip::state_type_e state) {
        if (state == vsomeip::state_type_e::ST_REGISTERED) {
            app_->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        }
    }

    void on_event(const std::shared_ptr<vsomeip::message>& msg) {
        std::cout << "Received event, size: " << msg->get_payload()->get_length() << "\n";
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(msg->get_payload()->get_data(), msg->get_payload()->get_length());
        app_->notify(RESPONSE_SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, payload);
    }

    void start() {
        std::cout << "Starting SampleClient..." << std::endl;
        app_->init();
        app_->register_state_handler([this](vsomeip::state_type_e state) { on_state(state); });

        std::set<vsomeip::eventgroup_t> groups{SAMPLE_EVENTGROUP_ID};
        app_->offer_event(RESPONSE_SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, groups);
        app_->offer_service(RESPONSE_SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

        app_->register_message_handler(
            SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID,
            [this](const std::shared_ptr<vsomeip::message>& msg) { on_event(msg); });

        std::set<vsomeip::eventgroup_t> its_groups;
        its_groups.insert(SAMPLE_EVENTGROUP_ID);
        app_->request_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, its_groups,
                            vsomeip::event_type_e::ET_EVENT);
        app_->subscribe(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENTGROUP_ID);
        app_->start();
    }

   private:
    std::shared_ptr<vsomeip::application> app_;
};

int main() {
    SampleClient client;
    client.start();
    return 0;
}
