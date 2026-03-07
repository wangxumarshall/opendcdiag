/*
 * Copyright 2025 Intel Corporation.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ARM64_CPUID_H
#define ARM64_CPUID_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

#define KUNPENG920_MIDR    0x480D0100

static inline uint64_t read_midr_from_sysfs(int cpu_id)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/regs/identification/midr_el1", cpu_id);

    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    uint64_t midr = 0;
    if (fscanf(f, "%lx", &midr) != 1)
        midr = 0;

    fclose(f);
    return midr;
}

static inline unsigned long get_hwcap(void)
{
    return getauxval(AT_HWCAP);
}

static inline unsigned long get_hwcap2(void)
{
    return getauxval(AT_HWCAP2);
}

struct cpuinfo_features {
    bool has_fp;
    bool has_simd;
    bool has_neon;
    bool has_crc32;
    bool has_crypto;
};

static inline struct cpuinfo_features parse_cpuinfo_features(void)
{
    struct cpuinfo_features features = {0};

    unsigned long hwcap = get_hwcap();

#ifdef AT_HWCAP
    features.has_fp = (hwcap & (1 << 0)) != 0;
    features.has_simd = (hwcap & (1 << 1)) != 0;
    features.has_neon = (hwcap & (1 << 12)) != 0;
    features.has_crc32 = (hwcap & (1 << 7)) != 0;
    features.has_crypto = (hwcap & (1 << 2)) != 0;
#endif

    return features;
}

static inline bool arm64_has_neon(void)
{
    struct cpuinfo_features f = parse_cpuinfo_features();
    return f.has_neon;
}

static inline bool arm64_has_crc32(void)
{
    struct cpuinfo_features f = parse_cpuinfo_features();
    return f.has_crc32;
}

static inline bool arm64_has_crypto(void)
{
    struct cpuinfo_features f = parse_cpuinfo_features();
    return f.has_crypto;
}

static inline bool arm64_has_fp(void)
{
    struct cpuinfo_features f = parse_cpuinfo_features();
    return f.has_fp;
}

static inline bool arm64_has_simd(void)
{
    struct cpuinfo_features f = parse_cpuinfo_features();
    return f.has_simd;
}

static inline bool is_kunpeng920(void)
{
    int cpu_count = sysconf(_SC_NPROCESSORS_CONF);
    if (cpu_count <= 0)
        cpu_count = 1;

    for (int i = 0; i < cpu_count; ++i) {
        uint64_t midr = read_midr_from_sysfs(i);
        if ((midr & 0xFFFF0000) == KUNPENG920_MIDR)
            return true;
    }
    return false;
}

#else

static inline uint64_t read_midr_from_sysfs(int cpu_id)
{
    (void)cpu_id;
    return 0;
}

static inline unsigned long get_hwcap(void)
{
    return 0;
}

static inline unsigned long get_hwcap2(void)
{
    return 0;
}

struct cpuinfo_features {
    bool has_fp;
    bool has_simd;
    bool has_neon;
    bool has_crc32;
    bool has_crypto;
};

static inline struct cpuinfo_features parse_cpuinfo_features(void)
{
    struct cpuinfo_features features = {0};
    return features;
}

static inline bool arm64_has_neon(void)
{
    return false;
}

static inline bool arm64_has_crc32(void)
{
    return false;
}

static inline bool arm64_has_crypto(void)
{
    return false;
}

static inline bool arm64_has_fp(void)
{
    return false;
}

static inline bool arm64_has_simd(void)
{
    return false;
}

static inline bool is_kunpeng920(void)
{
    return false;
}

#endif

#endif
