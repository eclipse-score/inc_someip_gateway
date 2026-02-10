# This file is part of the EB AdaptiveCore build environment.
#
# Copyright (C) Elektrobit Automotive GmbH
# All rights exclusively reserved for Elektrobit Automotive GmbH,
# unless otherwise expressly agreed.

# CMAKELINT DISABLE QMS 5.4
# Disable lint temporarily, some files are included in build test.
add_executable(socom_test
    client_tests.cpp
    connection_tests.cpp
    event_tests.cpp
    interface_compatibility_tests.cpp
    method_call_tests.cpp
    multi_connection_tests.cpp
    multiple_runtimes_tests.cpp
    mock_tests.cpp
    payload_tests.cpp
    runtime_tests.cpp
    server_tests.cpp
    service_interface_configuration_tests.cpp
    optional_ref_tests.cpp
    identity_tests.cpp
)
# CMAKELINT ENABLE QMS 5.4

target_link_libraries(socom_test
    PRIVATE
        ${ARA_SOCOM_MOCK_LIB}
        socom_test_framework
        Threads::Threads
        GTest::gmock
        GTest::gtest_main
)

eb_add_test(CONTEXT Unit DURATION Short NAME socom_test COMMAND socom_test)

# CMAKELINT DISABLE QMS 5.4
# Disable lint temporarily for the sake of build test.
add_executable(socom_deadlock_test
    client_tests.cpp
    server_tests.cpp
)
# CMAKELINT ENABLE QMS 5.4

target_link_libraries(socom_deadlock_test
    PRIVATE
        ${ARA_LOGGING_ADAPTER_LIB}
        ${ARA_SOCOM_MOCK_LIB}
        socom_test_with_deadlock_detection_framework
        Threads::Threads
        GTest::gmock
        GTest::gtest_main
)

eb_add_test(CONTEXT Unit DURATION Short NAME socom_deadlock_test COMMAND socom_deadlock_test)

add_executable(socom_advanced_test
    client_multi_threading_tests.cpp
    connection_multi_threading_tests.cpp
    maximum_payload_tests.cpp
    runtime_multi_threading_tests.cpp
)

target_link_libraries(socom_advanced_test
    PRIVATE
        ${ARA_SOCOM_LIB}
        socom_test_framework
        Threads::Threads
        GTest::gmock
        GTest::gtest_main
)

eb_add_test(
    CONTEXT Unit
    DURATION Short
    NAME socom_advanced_test
    COMMAND socom_advanced_test
)
