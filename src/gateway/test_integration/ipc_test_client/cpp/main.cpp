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
#include "sample_sender_receiver.h"

#include "score/gateway/assert_handler.h"
#include "score/mw/com/runtime.h"

#include <boost/program_options.hpp>

#include <cstdlib>
#include <iostream>

using namespace std::chrono_literals;

struct Params {
    score::cpp::optional<std::string> instance_manifest;
    score::cpp::optional<std::chrono::milliseconds> cycle_time;
    score::cpp::optional<unsigned long> cycle_num;
};

template<typename ParsedType, typename SavedType = ParsedType>
score::cpp::optional<SavedType>
GetValueIfProvided(const boost::program_options::variables_map& args, std::string arg_string)
{
    return (args.count(arg_string) > 0U) ? static_cast<SavedType>(args[arg_string].as<ParsedType>())
                                         : score::cpp::optional<SavedType>();
}

Params ParseCommandLineArguments(const int argc, const char** argv)
{
    namespace po = boost::program_options;

    po::options_description options;

    options.add_options()("help,h", "Display the help message");
    options.add_options()("num-cycles,n", po::value<std::size_t>()->default_value(0U),
                          "Number of cycles that are executed before determining success or "
                          "failure. 0 indicates no limit.");
    options.add_options()("cycle-time,t", po::value<std::size_t>(),
                          "Cycle time in milliseconds for sending/polling");
    options.add_options()("service_instance_manifest,s", po::value<std::string>(),
                          "Path to the com configuration file");

    po::variables_map args;
    const auto parsed_args =
        po::command_line_parser{argc, argv}
            .options(options)
            .style(po::command_line_style::unix_style | po::command_line_style::allow_long_disguise)
            .run();
    po::store(parsed_args, args);

    if (args.count("help") > 0U) {
        std::cerr << options << std::endl;
        std::exit(EXIT_SUCCESS);
    }

    return {GetValueIfProvided<std::string>(args, "service_instance_manifest"),
            GetValueIfProvided<std::size_t, std::chrono::milliseconds>(args, "cycle-time"),
            GetValueIfProvided<std::size_t>(args, "num-cycles")};
}

int main(const int argc, const char** argv)
{
    score::gateway::SetupAssertHandler();
    Params params = ParseCommandLineArguments(argc, argv);

    if (!params.cycle_num.has_value() || !params.cycle_time.has_value()) {
        std::cerr << "Number of cycles and cycle time should be specified" << std::endl;
        return EXIT_FAILURE;
    }

    if (params.instance_manifest.has_value()) {
        const std::string& manifest_path = params.instance_manifest.value();
        score::StringLiteral runtime_args[2u] = {"-service_instance_manifest",
                                                 manifest_path.c_str()};
        score::mw::com::runtime::InitializeRuntime(2, runtime_args);
    }

    const auto cycles = params.cycle_num.value();
    const auto cycle_time = params.cycle_time.value();

    score::gateway::EventSenderReceiver event_sender_receiver{};

    const auto instance_specifier_result =
        score::mw::com::InstanceSpecifier::Create(std::string{"xpad/cp60/MapApiLanesStamped"});
    if (!instance_specifier_result.has_value()) {
        std::cerr << "Invalid instance specifier, terminating." << std::endl;
        return EXIT_FAILURE;
    }
    const auto& instance_specifier = instance_specifier_result.value();

    return event_sender_receiver.RunAsProxy(instance_specifier, cycle_time, cycles);

    return EXIT_SUCCESS;
}
