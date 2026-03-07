# Copyright 2025 Intel Corporation.
# SPDX-License-Identifier: Apache-2.0

if host_machine.cpu_family() == 'aarch64'
    arm64_cpp_flags = [
        '-march=armv8.1-a+crc+crypto',
    ]

    arm64_tests = files(
        'neon_test.cpp',
        'crypto_test.cpp',
        'sdc_test.cpp',
    )

    arm64_tests_lib = static_library(
        'tests_arm64',
        arm64_tests,
        build_by_default: false,
        include_directories: [
            framework_incdir,
        ],
        dependencies: [
            boost_dep,
        ],
        cpp_args: [
            debug_c_flags,
            default_cpp_flags,
            default_cpp_warn,
            march_generic_flags,
            arm64_cpp_flags,
        ],
    )

    sandstone_tests += [
        arm64_tests_lib,
    ]
endif
