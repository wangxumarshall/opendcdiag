# OpenDCDiag ARM64 (鲲鹏920) 移植计划

## 一、架构差异分析

### x86 vs ARM64 关键差异

| 功能 | x86实现 | ARM64实现需求 |
|------|---------|---------------|
| **CPUID** | `cpuid`指令 | sysfs/getauxval/proc cpuinfo (用户态) |
| **SIMD** | AVX/AVX-512 | NEON (128-bit固定宽度) |
| **MSR** | RDMSR/WRMSR | sysfs/perf_event (用户态) |
| **拓扑检测** | CPUID + ACPI | ACPI PPTT + sysfs + device tree |
| **错误检测** | MCE | sysfs EDAC + ACPI APEI (用户态) |
| **内存模型** | TSO | Weak Memory Model + 内存屏障 |

---

## 二、ARM64实现方案

### 2.1 CPU特性检测 (用户态)

```c
// framework/device/cpu/arm64/arm64_cpuid.h
#ifndef ARM64_CPUID_H
#define ARM64_CPUID_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// 通过sysfs读取CPU ID
static inline uint64_t read_midr_from_sysfs(int cpu_id) {
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/regs/identification/midr_el1", cpu_id);

    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    uint64_t val;
    fscanf(fp, "0x%lx", &val);
    fclose(fp);
    return val;
}

// 通过getauxval()读取HWCAP
#include <sys/auxv.h>
static inline uint64_t get_arm64_hwcap(void) {
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);
    return hwcap | (hwcap2 << 32);
}

// 解析/proc/cpuinfo
static inline void parse_cpuinfo_features(uint64_t *features) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Features") || strstr(line, "flags")) {
            // 解析特性标志
        }
    }
    fclose(fp);
}

// CPU特性检测函数
static inline bool arm64_has_neon(void) {
    return getauxval(AT_HWCAP) & HWCAP_NEON;
}

static inline bool arm64_has_crc32(void) {
    return getauxval(AT_HWCAP) & HWCAP_CRC32;
}

static inline bool arm64_has_crypto(void) {
    return getauxval(AT_HWCAP) & HWCAP_CRYPTO;
}

#endif
```

### 2.2 特权级抽象层

```c
// framework/device/cpu/arm64/arm64_privilege.h
#ifndef ARM64_PRIVILEGE_H
#define ARM64_PRIVILEGE_H

#include <stdint.h>

typedef enum {
    ACCESS_USER_SPACE,       // 用户态接口 (sysfs/procfs)
    ACCESS_SYSFS,           // sysfs文件系统
    ACCESS_PROCFS,          // procfs文件系统
    ACCESS_PERF_EVENT,      // perf事件
    ACCESS_ACPI,            // ACPI表
} access_method_t;

struct hw_access_ops {
    int (*init)(void);
    uint64_t (*read_msr)(uint32_t addr);
    int (*write_msr)(uint32_t addr, uint64_t val);
    int (*read_ecc)(struct memory_error_stats *stats);
    int (*setup_ras_monitoring)(void);
    void (*cleanup)(void);
    const char *backend_name;
};

struct hw_access_ops *arm64_get_hw_access_ops(void);

#endif
```

### 2.3 SIMD抽象层

```c
// framework/fp_vectors/simd_abstraction.h
#ifndef SIMD_ABSTRACTION_H
#define SIMD_ABSTRACTION_H

#include <stdint.h>
#include <stddef.h>

// 向量宽度定义
#if defined(__x86_64__) && defined(__AVX512F__)
    #define SIMD_WIDTH 512
#elif defined(__aarch64__)
    #define SIMD_WIDTH 128
#else
    #define SIMD_WIDTH 128
#endif

#if defined(__aarch64__)
#include <arm_neon.h>

template<typename T>
struct SimdVector {
    static constexpr int elements = SIMD_WIDTH / (8 * sizeof(T));
    T data[SIMD_WIDTH / (8 * sizeof(T))];
};

// 512位操作需要4条NEON指令
template<>
inline void simd_store<int32_t, 512>(int32_t *ptr,
                                      const SimdVector<int32_t> &vec) {
    for (size_t i = 0; i < 4; i++) {
        vst1q_s32(ptr + i * 4, vld1q_s32(&vec.data[i * 4]));
    }
}

template<>
inline SimdVector<int32_t> simd_load<int32_t, 512>(const int32_t *ptr) {
    SimdVector<int32_t> vec;
    for (size_t i = 0; i < 4; i++) {
        vec.data[i * 4] = *(int32x4_t *)(ptr + i * 4);
    }
    return vec;
}

#endif
#endif
```

**SIMD映射对照表**:

| x86 AVX-512 | ARM64 NEON | 说明 |
|-------------|-----------|------|
| `_mm512_load_epi32` | 4x `vld1q_s32` | 512位需4条128位指令 |
| `_mm512_store_epi32` | 4x `vst1q_s32` | 512位需4条128位指令 |
| `_mm512_add_epi32` | 4x `vaddq_s32` | 向量加法 |
| `_mm512_set_epi32` | 4x `vdupq_n_s32` | 广播值 |
| `_mm512_reduce_add_epi32` | `vaddvq_s32` + 循环 | 归约求和 |

### 2.4 拓扑检测

```c
// framework/device/cpu/arm64/arm64_topology.cpp

enum topology_source {
    TOPO_SOURCE_ACPI_PPTT,    // ACPI PPTT表 (优先)
    TOPO_SOURCE_SYSFS,         // Linux sysfs
    TOPO_SOURCE_DEVICE_TREE,   // 设备树
    TOPO_SOURCE_PROC_CPUINFO,  // /proc/cpuinfo (降级)
};

class Arm64TopologyDetector {
public:
    void detect(const LogicalProcessorSet &enabled_cpus) {
        if (detect_via_acpi_pptt()) return;
        if (detect_via_sysfs()) return;
        if (detect_via_device_tree()) return;
        detect_via_proc_cpuinfo();
    }

private:
    bool detect_via_acpi_pptt();
    bool detect_via_sysfs();
    bool detect_via_device_tree();
    void parse_cache_topology_sysfs(int cpu, int level);
};
```

### 2.5 RAS/ECC错误检测

```c
// framework/device/cpu/arm64/arm64_ras.h
#ifndef ARM64_RAS_H
#define ARM64_RAS_H

#include <stdint.h>
#include <dirent.h>

struct memory_error_stats {
    uint64_t ce_count;    // Correctable errors
    uint64_t ue_count;    // Uncorrectable errors
    uint64_t ce_fatal;
    uint64_t ue_fatal;
    char dimm_name[64];
    int dimm_id;
};

// 通过sysfs读取EDAC错误计数
static inline int read_edac_errors(const char *mc_path,
                                   struct memory_error_stats *stats) {
    char path[256];
    FILE *fp;

    snprintf(path, sizeof(path), "%s/ce_count", mc_path);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->ce_count); fclose(fp); }

    snprintf(path, sizeof(path), "%s/ue_count", mc_path);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->ue_count); fclose(fp); }

    return 0;
}

static inline bool check_apei_available(void) {
    return access("/sys/firmware/acpi/apei/einj", F_OK) == 0 ||
           access("/sys/firmware/acpi/apei/erst", F_OK) == 0;
}

#endif
```

### 2.6 多层级ECC检测

```c
// framework/device/cpu/arm64/kunpeng920_ecc.cpp

enum ecc_backend {
    ECC_BACKEND_EDAC,           // Linux EDAC子系统 (优先)
    ECC_BACKEND_ACPI_APEI,      // ACPI APEI/ERST
    ECC_BACKEND_VENDOR_DRIVER,  // 厂商专用驱动
    ECC_BACKEND_NONE,           // 不支持
};

class Kunpeng920EccDetector {
public:
    int init() {
        backend_ = detect_backend();
        if (backend_ == ECC_BACKEND_NONE) {
            return -ENOTSUP;
        }
        return 0;
    }

    int read_errors(struct memory_error_stats *stats) {
        switch (backend_) {
        case ECC_BACKEND_EDAC:    return read_edac_errors(stats);
        case ECC_BACKEND_ACPI_APEI: return read_apei_errors(stats);
        case ECC_BACKEND_VENDOR_DRIVER: return read_vendor_errors(stats);
        default: return -ENOTSUP;
        }
    }

private:
    enum ecc_backend detect_backend() {
        if (check_edac_available()) return ECC_BACKEND_EDAC;
        if (check_apei_available()) return ECC_BACKEND_ACPI_APEI;
        if (check_vendor_driver()) return ECC_BACKEND_VENDOR_DRIVER;
        return ECC_BACKEND_NONE;
    }

    bool check_edac_available() {
        DIR *dir = opendir("/sys/devices/system/edac/mc");
        if (dir) { closedir(dir); return true; }
        return false;
    }

    bool check_apei_available();
    bool check_vendor_driver();
    enum ecc_backend backend_;
};
```

### 2.7 SDC静默错误检测

```c
// framework/device/cpu/arm64/arm64_sdc_detect.cpp

enum sdc_detection_method {
    SDC_METHOD_HW_ECC,      // 硬件ECC (优先)
    SDC_METHOD_CRC32,       // CRC32校验
    SDC_METHOD_CRC64,       // CRC64校验
    SDC_METHOD_CHECKSUM,    // 校验和
};

struct SdcConfig {
    bool enable_ecc_check;
    bool enable_software_check;
    uint64_t ce_threshold;
    uint64_t ue_threshold;
    bool enable_injection;
};

class SdcDetector {
public:
    int init(const SdcConfig &config) {
        config_ = config;

        if (config_.enable_ecc_check) {
            ecc_detector_ = new Kunpeng920EccDetector();
            if (ecc_detector_->init() == 0) {
                methods_.push_back(SDC_METHOD_HW_ECC);
            }
        }

        if (config_.enable_software_check) {
            methods_.push_back(SDC_METHOD_CRC32);
            methods_.push_back(SDC_METHOD_CRC64);
        }

        return methods_.empty() ? -ENOTSUP : 0;
    }

    bool detect() {
        for (auto method : methods_) {
            if (run_detection(method)) return true;
        }
        return false;
    }

private:
    bool run_detection(sdc_detection_method method);
    bool detect_via_crc32();
    SdcConfig config_;
    Kunpeng920EccDetector *ecc_detector_;
    std::vector<sdc_detection_method> methods_;
};
```

---

## 三、编译配置

### 3.1 ARM64编译选项

```python
# meson.build
if host_machine.cpu_family() == 'aarch64'
    march_flags = [
        '-march=armv8.1-a+crc+crypto',
        '-mtune=tsv110',
    ]

    if cc.has_argument('-mmemory-model=lp64')
        march_flags += '-mmemory-model=lp64'
    endif

    add_project_arguments([
        '-D__aarch64__=1',
    ], language: ['c', 'cpp'])

    message('Building for ARM64 (Kunpeng 920)')
endif
```

### 3.2 交叉编译配置

```ini
# meson-cross-arm64.ini
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
```

---

## 四、移植步骤

### 阶段1: 基础设施 (Week 1-2)

新建文件:
```
framework/device/cpu/arm64/
├── arm64_cpu_device.h
├── arm64_cpu_device.cpp
├── arm64_topology.cpp
├── arm64_cpuid.h
├── arm64_features.h
├── arm64_privilege.h
├── arm64_ras.h
├── arm64_ras.cpp
├── kunpeng920_ecc.h
├── kunpeng920_ecc.cpp
├── arm64_sdc_detect.h
├── arm64_sdc_detect.cpp
├── neon_vectors.h
└── meson.build
```

### 阶段2: 核心功能 (Week 2-4)

- CPU特性检测: sysfs/getauxval/proc cpuinfo
- 拓扑检测: ACPI PPTT + sysfs + device tree
- SIMD: NEON抽象层实现

### 阶段3: SDC检测 (Week 4-6)

- ARM64 RAS: EDAC + APEI + 厂商驱动
- ECC: 多层级检测架构
- SDC: 多算法检测

### 阶段4-5: 测试与验证 (Week 6-8)

---

## 五、需要修改的文件

| 文件 | 变更 |
|------|------|
| `meson.build` | 添加ARM64架构检测和编译选项 |
| `meson_options.txt` | 添加arm64特定选项 |
| `framework/device/device.h` | 添加ARM64设备类型条件编译 |
| `framework/device/cpu/cpu_device.h` | 添加ARM64设备定义 |
| `framework/device/cpu/topology.cpp` | 添加ARM64分支 |
| `framework/sandstone.h` | 条件编译适配 |
| `framework/random.cpp` | 替换x86 AES-NI为ARM CRYPTO |
| `framework/selftest.cpp` | ARM64自测试 |
| `tests/cpu/meson.build` | ARM64测试集 |

---

## 六、鲲鹏920硬件特性

| 特性 | 支持 | 用户态访问 |
|------|------|-----------|
| ARMv8.1-A | ✓ | - |
| AArch64 | ✓ | - |
| NEON | ✓ | getauxval |
| CRC32 | ✓ | getauxval |
| Crypto | ✓ | getauxval |
| RAS | ✓ | sysfs EDAC |
| PMU | ✓ | perf_event |

---

## 七、内存模型与原子操作

```c
// 内存屏障
#if defined(__aarch64__)
    #define mb() __asm__ __volatile__("dmb ish" ::: "memory")
    #define rmb() __asm__ __volatile__("dmb ishld" ::: "memory")
    #define wmb() __asm__ __volatile__("dmb ishst" ::: "memory")
#endif

// 原子操作
#if defined(__aarch64__)
    static inline int atomic_add(int *ptr, int val) {
        int old, new_val;
        do {
            old = *ptr;
            new_val = old + val;
        } while (__sync_val_compare_and_swap(ptr, old, new_val) != old);
        return old;
    }
#endif
```

---

## 八、风险与测试计划

### 主要风险

1. SIMD覆盖不完整 - 鲲鹏920不支持SVE，需使用NEON
2. 错误检测接口差异 - x86 MCE vs ARM RAS机制
3. 第三方库依赖 - Eigen ARM64优化

### 测试验证计划

| 阶段 | 测试内容 |
|------|---------|
| Week 7 | 单元测试: CPUID, 拓扑检测, RAS, EDAC |
| Week 8 | 集成测试: 自测试, 基础测试 |
| Week 9 | 功能测试: SIMD测试, SDC检测 |
| Week 10 | 性能测试与稳定性测试 |
