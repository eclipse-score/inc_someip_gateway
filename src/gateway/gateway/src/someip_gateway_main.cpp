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
#include "gateway.h"

#include "score/mw/com/runtime.h"
#include "score/os/glob_impl.h"
#include <score/gateway/assert_handler.h>

#include <boost/program_options.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>

using namespace std::chrono_literals;

struct Params {
    std::optional<std::string> instance_manifest;
    std::optional<std::chrono::milliseconds> cycle_time;
    std::optional<unsigned long> cycle_num;
    std::string plugin_path;
    std::optional<std::string> someip_manifest_dir;
    std::string network_interface;
    std::string ip_address;
};

template<typename ParsedType, typename SavedType = ParsedType>
std::optional<SavedType> GetValueIfProvided(const boost::program_options::variables_map& args,
                                            std::string arg_string)
{
    return (args.count(arg_string) > 0U) ? static_cast<SavedType>(args[arg_string].as<ParsedType>())
                                         : std::optional<SavedType>();
}

namespace std {

std::ostream& operator<<(std::ostream& out, std::vector<std::string> const& vec)
{
    out << "[";
    for (std::size_t i = 0; i < vec.size(); ++i) {
        out << vec[i];
        if (i < vec.size() - 1U) {
            out << ", ";
        }
    }
    out << "]";
    return out;
}

} // namespace std

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
    options.add_options()("plugin-path,p", po::value<std::string>(),
                          "Path to the someip network plugin");
    options.add_options()("xsomeip-manifest-dir,x", po::value<std::string>(),
                          "Path to SOME/IP manifest file(s)");
    options.add_options()("interface,i", po::value<std::string>()->default_value("lo"),
                          "Network interface to bind SOME/IP communication to");
    options.add_options()("address,a", po::value<std::string>()->default_value("::1"),
                          "IP address to bind SOME/IP communication to");

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
            GetValueIfProvided<std::size_t>(args, "num-cycles"),
            args["plugin-path"].as<std::string>(),
            GetValueIfProvided<std::string>(args, "xsomeip-manifest-dir"),
            args["interface"].as<std::string>(),
            args["address"].as<std::string>()};
}

std::vector<std::string> get_manifests(std::optional<std::string> const& manifest_dir)
{
    if (!manifest_dir.has_value()) {
        return {};
    }

    score::os::GlobImpl glob;
    auto const matches = glob.Match(manifest_dir.value() + "/*", score::os::Glob::Flag::kErr);

    if (!matches.has_value()) {
        return {};
    }

    return matches.value().paths;
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

    auto const manifests = get_manifests(params.someip_manifest_dir);
    std::cout << "Found SOME/IP manifests: " << manifests << std::endl;
    auto create_result = score::gateway::Gateway::create(
        params.plugin_path, params.network_interface, params.ip_address, manifests);

    if (!create_result.has_value()) {
        std::cerr << "Unable to create gateway: " << create_result.error() << ", terminating."
                  << std::endl;
        return EXIT_FAILURE;
    }

    auto& event_sender_receiver = create_result.value();

    return event_sender_receiver.run(cycle_time, cycles);

    return EXIT_SUCCESS;
}
