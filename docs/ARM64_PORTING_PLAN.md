# OpenDCDiag ARM64 (鲲鹏920) 移植计划

## 一、架构差异分析总结

### x86 vs ARM64 关键差异

| 功能 | x86实现 | ARM64实现需求 |
|------|---------|---------------|
| **CPUID** | `cpuid`指令 | ARM ID registers (MIDR, ID_AA64*) |
| **SIMD** | AVX/AVX-512 | NEON (SVE/SVE2 for Kunpeng 920) |
| **MSR** | RDMSR/WRMSR | PMCCNTR, PMU registers |
| **拓扑检测** | CPUID + ACPI | /proc/cpuinfo, /sys/devices |
| **错误检测** | MCE | ARMv8 RAS, SEA/SEI异常 |
| **XSAVE** | XSAVE/XRSTOR | SVE Z-registers lazy save |

---

## 二、移植步骤详细规划

### 阶段1: 基础设施适配 (Week 1-2)

#### 1.1 创建ARM64设备抽象层

**新建文件:**
```
framework/device/cpu/arm64/
├── arm64_cpu_device.h      # ARM64 CPU设备定义
├── arm64_cpu_device.cpp    # ARM64 CPU设备实现
├── arm64_topology.cpp      # ARM64拓扑检测
├── arm64_cpuid.cpp         # ARM ID registers访问
└── meson.build
```

**关键实现 - ARM ID Registers访问:**

```c
// framework/device/cpu/arm64/arm64_cpuid.h
#ifndef ARM64_CPUID_H
#define ARM64_CPUID_H

#include <stdint.h>

// ARM64 ID Registers - 实现CPU特性检测
static inline uint64_t read_midr_el1(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, MIDR_EL1" : "=r"(val));
    return val;
}

static inline uint64_t read_id_aa64pfr0_el1(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, ID_AA64PFR0_EL1" : "=r"(val));
    return val;
}

static inline uint64_t read_id_aa64pfr1_el1(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, ID_AA64PFR1_EL1" : "=r"(val));
    return val;
}

static inline uint64_t read_id_aa64isar0_el1(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, ID_AA64ISAR0_EL1" : "=r"(val));
    return val;
}

static inline uint64_t read_id_aa64isar1_el1(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, ID_AA64ISAR1_EL1" : "=r"(val));
    return val;
}

// 鲲鹏920识别 - Huawei TSV110 (Kunpeng 920)
#define KUNPENG_920_MIDR 0x480D0100

#endif
```

#### 1.2 创建ARM64 CPU设备定义

```c
// framework/device/cpu/arm64/arm64_cpu_device.h
#ifndef ARM64_CPU_DEVICE_H
#define ARM64_CPU_DEVICE_H

#include "cpu_device.h"

struct arm64_cpu_info_t {
    uint64_t microcode;
    int cpu_number;

    // ARM64拓扑信息
    int8_t thread_id;
    int16_t core_id;
    int16_t cluster_id;
    int16_t numa_id;
    int16_t package_id;

    // ARM特定信息
    uint64_t midr_el1;        // Main ID Register
    uint64_t id_aa64pfr0;     // Processor Feature Register 0
    uint64_t id_aa64pfr1;    // Processor Feature Register 1

    // 缓存信息
    struct cache_info_t cache[3];

    // 核心类型 (P-core / E-core for ARM big.LITTLE)
    NativeCoreType native_core_type;
};

typedef struct arm64_cpu_info_t device_info_t;
extern struct arm64_cpu_info_t *device_info;

#endif
```

#### 1.3 适配Meson构建系统

**修改 meson.build:**

```python
# 添加ARM64架构支持
if host_machine.cpu_family() == 'aarch64'
    march_base = 'generic'
    message('Building for ARM64 (AArch64)')
elif host_machine.cpu_family() == 'x86_64'
    # 现有代码
endif

# 添加ARM64平台特定的编译选项
if target_machine.cpu_family() == 'aarch64'
    # ARM64特定flags
    common_flags += [
        '-march=armv8.1-a+native',
        '-mtune=cortex-a72',  # 或鲲鹏920的TSV110
    ]
endif
```

**新增 meson-cross-arm64.ini:**

```ini
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
```

---

### 阶段2: 核心功能移植 (Week 2-4)

#### 2.1 CPU特性检测适配

```c
// framework/device/cpu/arm64/arm64_features.h
// ARM64 CPU特性定义

#define ARM64_FEATURE_NEON       (1ULL << 0)
#define ARM64_FEATURE_CRC32      (1ULL << 1)
#define ARM64_FEATURE_CRYPTO     (1ULL << 2)
#define ARM64_FEATURE_SHA1        (1ULL << 3)
#define ARM64_FEATURE_SHA2        (1ULL << 4)
#define ARM64_FEATURE_AES         (1ULL << 5)
#define ARM64_FEATURE_SVE         (1ULL << 6)    // Scalable Vector Extension
#define ARM64_FEATURE_SVE2        (1ULL << 7)
#define ARM64_FEATURE_RAS         (1ULL << 8)    // Reliability/Availability
#define ARM64_FEATURE_PMU         (1ULL << 9)    // Performance Monitors
#define ARM64_FEATURE_DEBUG       (1ULL << 10)
#define ARM64_FEATURE_VHE         (1ULL << 11)  // Virtualization
#define ARM64_FEATURE_SVE2_FP16   (1ULL << 12)
#define ARM64_FEATURE_SVE2_I8MM   (1ULL << 13)  // Int8 Matrix Multiply
#define ARM64_FEATURE_SVE2_BF16   (1ULL << 14)  // BFloat16

// 鲲鹏920特性
#define KUNPENG920_FEATURES (ARM64_FEATURE_NEON | ARM64_FEATURE_CRC32 | \
    ARM64_FEATURE_CRYPTO | ARM64_FEATURE_SHA1 | ARM64_FEATURE_SHA2 | \
    ARM64_FEATURE_AES | ARM64_FEATURE_PMU | ARM64_FEATURE_RAS)
```

#### 2.2 拓扑检测适配

```c
// framework/device/cpu/arm64/arm64_topology.cpp
// 基于Linux /sys和/proc的拓扑检测

class Arm64TopologyDetector {
public:
    void detect(const LogicalProcessorSet &enabled_cpus) {
        detect_via_sysfs();
        detect_numa();
        detect_cache_info();
        detect_core_type();  // P-core vs E-core
    }

private:
    bool detect_via_sysfs(Topology::Thread *info);
    void detect_numa();      // /sys/devices/system/node/
    void detect_cache_info(); // /sys/devices/system/cpu/cpu*/cache/
    void detect_core_type();  // /sys/devices/system/cpu/cpu*/cpu_capacity

    bool is_kunpeng920();
};
```

#### 2.3 替换x86 Intrinsics

**文件对应关系:**

| x86 Intrinsics | ARM64 NEON/SVE等价 |
|---------------|-------------------|
| `_mm512_load_epi32` | `vld1q_s32` |
| `_mm512_store_epi32` | `vst1q_s32` |
| `_mm512_add_epi32` | `vaddq_s32` |
| `_mm512_set_epi32` | `vdupq_n_s32` |
| `_mm256_*` | NEON 128-bit |
| AVX-512 (512-bit) | SVE (可变长度) |

**创建NEON兼容层:**

```c
// framework/fp_vectors/neon_vectors.h
#ifndef NEON_VECTORS_H
#define NEON_VECTORS_H

#include <arm_neon.h>

// 为不支持SVE的CPU提供NEON fallback
// 为支持SVE的CPU提供SVE实现

#if defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#define MAX_VECTOR_BITS svcnt()
#else
#define MAX_VECTOR_BITS 128
#endif

// 通用向量操作封装
template<typename T>
struct NeonVector {
    static constexpr size_t element_size = sizeof(T);
    // ... 实现
};

#endif
```

---

### 阶段3: SDC静默错误检测适配 (Week 4-6)

#### 3.1 ARM64 RAS错误检测

```c
// framework/device/cpu/arm64/arm64_ras.h
#ifndef ARM64_RAS_H
#define ARM64_RAS_H

#include <signal.h>
#include <ucontext.h>

// ARM64 RAS (Reliability/Availability) 错误类型
enum Arm64ErrorType {
    ARM64_ERROR_NONE = 0,
    ARM64_ERROR_UC,          // Uncorrectable
    ARM64_ERROR_CE,          // Corrected
    ARM64_ERROR_DEFERRED,   // Deferred/Retry
    ARM64_ERROR_SEA,        // Synchronous External Abort
    ARM64_ERROR_SEI,        // Synchronous Error Interrupt
};

// 错误状态结构
struct Arm64ErrorState {
    enum Arm64ErrorType type;
    uint64_t fault_address;
    uint64_t syndrome;
    int source;  // 内存控制器, CPU内部, etc.
};

// SEA/SEI信号处理器
void arm64_ras_init(void);
void arm64_ras_handler(int sig, siginfo_t *info, ucontext_t *uc);

#endif
```

#### 3.2 鲲鹏920 ECC内存检测

```c
// framework/device/cpu/arm64/kunpeng920_ecc.h
#ifndef KUNPENG920_ECC_H
#define KUNPENG920_ECC_H

#include <stdint.h>

// 鲲鹏920 DDR控制器错误报告
// 通过ACPI APEI (ARM Processor Error Interface) 获取

#define KUNPENG920_EDAC_PATH "/sys/devices/system/edac/"

struct Kunpeng920MemoryError {
    uint64_t ce_count;    // Correctable errors
    uint64_t ue_count;    // Uncorrectable errors
    uint32_t dimm_slot;
    uint32_t channel;
};

// 读取内存错误统计
int kunpeng920_read_memory_errors(struct Kunpeng920MemoryError *err);

// SDC检测配置
struct SdcConfig {
    bool enable_ecc_check;
    bool enable_ras_check;
    bool enable_memory_parity;
    uint64_t ce_threshold;   // Correctable error阈值
    uint64_t ue_threshold;   // Uncorrectable error阈值
};

#endif
```

#### 3.3 SDC检测核心逻辑

```c
// framework/device/cpu/arm64/arm64_sdc_detect.cpp
#include "arm64_ras.h"
#include "kunpeng920_ecc.h"

class SdcDetector {
public:
    SdcDetector();
    void init(const SdcConfig &config);
    bool detect();  // 执行一次检测

    // 静默错误检测 - 计算冗余校验
    template<typename T>
    bool verify_data_parity(const T *expected, const T *actual, size_t count);

private:
    SdcConfig m_config;
    uint64_t m_total_ces;
    uint64_t m_total_ues;

    // 鲲鹏920特定
    std::vector<Kunpeng920MemoryError> m_memory_errors;
};

// 数据校验 - 使用ARM CRC32指令
template<typename T>
bool SdcDetector::verify_data_parity(const T *expected, const T *actual, size_t count) {
    uint32_t crc_expected = 0;
    uint32_t crc_actual = 0;

    for (size_t i = 0; i < count; i++) {
        crc_expected = __crc32cd(crc_expected, expected[i]);
        crc_actual = __crc32cd(crc_actual, actual[i]);
    }

    return crc_expected == crc_actual;
}
```

---

### 阶段4: 测试适配 (Week 6-7)

#### 4.1 创建ARM64测试集

```c
// tests/cpu/arm64/neon_test.cpp
// NEON SIMD测试

#include "sandstone.h"

static int neon_add_init(struct test *test) { ... }
static int neon_add_run(struct test *test, int cpu) {
    // 使用NEON intrinsics
    uint32x4_t a = vld1q_u32(ptr_a);
    uint32x4_t b = vld1q_u32(ptr_b);
    uint32x4_t result = vaddq_u32(a, b);
    vst1q_u32(ptr_result, result);
    // 比较结果
}
DECLARE_TEST(neon_add, "NEON SIMD addition test")
    .test_init = neon_add_init,
    .test_run = neon_add_run,
END_DECLARE_TEST
```

#### 4.2 SIMD测试适配矩阵

| x86测试 | ARM64对应测试 | 实现方式 |
|---------|--------------|---------|
| vector_add (AVX-512) | neon_vector_add | NEON 128-bit |
| eigen_gemm | eigen_gemm_neon | NEON矩阵乘法 |
| openssl_sha | openssl_sha_neon | ARM CRYPTO扩展 |
| zlib | zlib_neon | NEON优化 |

---

### 阶段5: 构建与测试 (Week 7-8)

#### 5.1 交叉编译配置

```bash
# 安装ARM64工具链 (Ubuntu)
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# 构建命令
meson builddir-arm64 --cross-file meson-cross-arm64.ini
ninja -C builddir-arm64
```

#### 5.2 测试验证用例

```bash
# 基本功能测试
./opendcdiag --list
./opendcdiag --beta -e self_test

# ARM64特定测试
./opendcdiag --beta -e arm64_* --list

# SDC检测测试
./opendcdiag --beta -e sdc_detect

# 性能测试
./opendcdiag --beta --duration=30s -e neon_*
```

---

## 三、关键代码变更点汇总

### 需要修改的核心文件

| 文件 | 变更类型 | 描述 |
|------|---------|------|
| `meson.build` | 修改 | 添加ARM64架构检测和编译选项 |
| `meson_options.txt` | 修改 | 添加arm64特定选项 |
| `framework/device/device.h` | 修改 | 添加ARM64设备类型条件编译 |
| `framework/device/cpu/cpu_device.h` | 修改 | 添加ARM64设备定义 |
| `framework/device/cpu/topology.cpp` | 修改 | 添加ARM64分支 |
| `framework/sandstone.h` | 修改 | 条件编译适配 |
| `framework/random.cpp` | 修改 | 替换x86 AES-NI为ARM CRYPTO |
| `framework/selftest.cpp` | 修改 | ARM64自测试 |
| `tests/cpu/meson.build` | 修改 | ARM64测试集 |

### 需要新建的文件

```
framework/device/cpu/arm64/
├── arm64_cpu_device.h
├── arm64_cpu_device.cpp
├── arm64_topology.cpp
├── arm64_cpuid.h
├── arm64_features.h
├── arm64_ras.h
├── arm64_ras.cpp
├── kunpeng920_ecc.h
├── kunpeng920_ecc.cpp
├── arm64_sdc_detect.h
├── arm64_sdc_detect.cpp
├── neon_vectors.h
├── meson.build

tests/cpu/arm64/
├── neon_add.cpp
├── arm64_crypto.cpp
├── sdc_detect.cpp
├── meson.build
```

---

## 四、鲲鹏920特定优化

### 鲲鹏920硬件特性

| 特性 | 支持情况 |
|------|---------|
| ARMv8.1-A | ✓ |
| AArch64 | ✓ |
| NEON | ✓ |
| CRC32 | ✓ |
| Crypto (AES/SHA) | ✓ |
| SVE | ✗ (无) |
| RAS | ✓ |
| PMU | ✓ |
| 32/64核 | ✓ (具体看型号) |

### 鲲鹏920特定配置

```c
// kunpeng920_config.h
#define KUNPENG920_CPU_NAME "Kunpeng 920"
#define KUNPENG920_MODEL_0   0x480D0100  // TSV110-xxxx
#define KUNPENG920_MODEL_1   0x480D0200  // 鲲鹏920 7nm

// 鲲鹏920优化参数
#define KUNPENG920_L1_DCACHE_SIZE   (32 * 1024)
#define KUNPENG920_L1_ICACHE_SIZE   (32 * 1024)
#define KUNPENG920_L2_CACHE_SIZE    (512 * 1024)
#define KUNPENG920_L3_CACHE_SIZE    (64 * 1024 * 1024)

// 鲲鹏920turbo频率
#define KUNPENG920_MAX_FREQ_MHZ    2800
#define KUNPENG920_BASE_FREQ_MHZ   2600
```

---

## 五、风险与挑战

### 主要风险

1. **SIMD覆盖不完整**
   - 鲲鹏920不支持SVE，需使用NEON
   - 需要为每个x86 SIMD测试创建对应NEON版本

2. **错误检测接口差异**
   - x86 MCE vs ARM RAS机制完全不同
   - 需要重新实现SDC检测逻辑

3. **第三方库依赖**
   - Eigen可能有ARM64优化问题
   - 需要验证所有依赖的ARM64兼容性

4. **KVM虚拟化支持**
   - 当前KVM仅支持x86
   - 需要评估ARM64 KVM支持需求

### 建议的测试验证计划

| 阶段 | 测试类型 | 覆盖内容 |
|------|---------|---------|
| Week 8 | 单元测试 | CPUID, 拓扑检测, RAS |
| Week 9 | 集成测试 | 自测试, 基础测试 |
| Week 10 | 功能测试 | SIMD测试, 错误注入 |
| Week 11 | 性能测试 | 吞吐量, 延迟对比 |
| Week 12 | 稳定性测试 | 72小时压力测试 |

---

## 六、x86架构特定依赖项详细分析

### 1. x86特定的汇编代码

#### 1.1 汇编源文件 (.S)

| 文件路径 | 功能描述 |
|---------|---------|
| `framework/sandstone_sections.S` | 平台特定的节区符号定义：macOS使用`__DATA`段添加测试和测试组符号，Linux使用ELF格式的GNU-stack节 |

#### 1.2 内联汇编代码 (__asm__/asm关键字)

| 文件路径 | 行号 | 功能描述 |
|---------|------|---------|
| `splitlock_detect.c:27` | 27 | Windows平台：使用`lock xchg`指令检测split lock |
| `splitlock_detect.c:30` | 30 | Unix平台：使用`lock xchg`指令检测split lock |
| `selftest.cpp:768` | 768 | APX状态初始化：`.byte 0xd5, 0x10`编码 |
| `selftest.cpp:777` | 777 | AVX-512状态初始化：`vpternlogd`指令 |
| `selftest.cpp:780` | 780 | AVX状态初始化：`vpcmpeqb`指令 |
| `selftest.cpp:792` | 792 | AMX Tile加载：`ldtilecfg`指令 |
| `selftest.cpp:793` | 793 | AMX Tile数据加载：`tileloadd`指令 |
| `selftest.cpp:820` | 820 | ARM64：`udf #0x1234`未定义指令 |
| `selftest.cpp:981` | 981 | x86-64：测试`movabs`指令 |
| `selftest.cpp:1084` | 1084 | CPUID指令测试 |
| `sandstone.cpp:842` | 842 | x86-64寄存器操作：`movl %0, %%ebx` |
| `cpuid_internal.h:200` | 200 | XGETBV指令获取XCR0寄存器 |
| `amx_common.h:32-64` | 32-64 | AMX指令宏定义 |

---

### 2. CPUID相关实现

#### 2.1 核心CPUID检测文件

| 文件路径 | 功能描述 |
|---------|---------|
| `framework/device/cpu/cpuid_internal.h` | CPUID功能检测的核心实现，包含x86 CPU特性解析 |
| `framework/device/cpu/topology.cpp` | 通过CPUID检测CPU拓扑结构 |
| `framework/device/cpu/premain.cpp` | CPU检测初始化和特性验证 |

#### 2.2 CPUID函数调用位置

| 文件路径 | 行号 | 功能 |
|---------|------|------|
| `topology.cpp:988` | 988 | `__cpuid(1, ...)` - 获取CPU基本信息 |
| `topology.cpp:1011` | 1011 | `__cpuid_count(0x04, ...)` - 检测缓存信息 |
| `topology.cpp:1054` | 1054 | `__cpuid_count(0x1a, 0, ...)` - 检测混合CPU类型 |
| `topology.cpp:1079` | 1079 | `__cpuid(0, ...)` - 获取最大CPUID leaf |
| `amx_common.h:100` | 100 | `__get_cpuid_count(0x1d, 0, ...)` - AMX检测 |
| `amx_common.h:109` | 109 | `__cpuid_count(0x1d, 1, ...)` - AMX Palette1信息 |
| `amx_common.h:122` | 122 | `__get_cpuid_count(0x1e, 0, ...)` - TMUL信息 |

---

### 3. MSR (Model Specific Register) 相关代码

#### 3.1 MSR访问接口

| 文件路径 | 功能描述 |
|---------|---------|
| `framework/sysdeps/linux/msr.c` | Linux MSR访问实现，通过/dev/msr设备读写 |
| `framework/sandstone.h:470-489` | MSR读写API声明 |

#### 3.2 MSR使用位置

| 文件路径 | 行号 | MSR地址 | 功能描述 |
|---------|------|---------|---------|
| `interrupt_monitor.hpp:17` | 17 | 0x34 | MSR_SMI_COUNT - SMI计数 |
| `topology.cpp:1189` | 1189 | 0x8B | 读取微码版本 |
| `topology.cpp:1278` | 1278 | 0x4F | MSR_PPIN - PPIN |
| `effective_cpu_freq.hpp:20` | 20 | 0xE8 | APERF_MSR - 实际性能计数 |
| `effective_cpu_freq.hpp:21` | 21 | 0xE7 | MPERF_MSR - 最大性能计数 |

---

### 4. x86特定Intrinsics (_mm_* 系列)

#### 4.1 使用Intrinsics的文件

| 文件路径 | 使用的Intrinsics |
|---------|-----------------|
| `tests/examples/vector_add.c` | `_mm512_load_epi32`, `_mm512_add_epi32`, `_mm512_store_epi32` |
| `framework/selftest.cpp:756-762` | `_mm_setr_epi32`, `_mm_set_ps`, `_mm_set_pd`, `_mm_set1_epi32` |
| `framework/random.cpp:310-319` | `_mm_loadu_si128`, `_mm_store_si128`, `_mm_xor_si128`, `_mm_aesenc_si128` |
| `framework/random.cpp:359-373` | `_mm_cvtsi128_si32`, `_mm_cvtsi128_si64`, `_mm_extract_epi64` |

---

### 5. 特定于x86的头文件

| 文件路径 | 包含的头文件 | 功能 |
|---------|-------------|------|
| `tests/examples/vector_add.c:15` | `<immintrin.h>` | AVX-512 intrinsic函数 |
| `framework/sandstone.h:20` | `<immintrin.h>` | SIMD intrinsic函数 |
| `framework/random.cpp:23,45` | `<cpuid.h>`, `<immintrin.h>` | CPUID和AES intrinsics |
| `framework/fp_vectors/Floats.h:18` | `<immintrin.h>` | 浮点向量操作 |
| `amx_common.h:10-12` | `<cpuid.h>`, `<immintrin.h>` | AMX支持 |

---

### 6. 设备抽象层中的x86特定实现

#### 6.1 XSAVE状态管理

| 文件路径 | 功能描述 |
|---------|---------|
| `framework/xsave_states.h` | XSAVE状态枚举：X87, SSE, AVX, AVX512, AMX, APX |
| `framework/sandstone_context_dump.cpp` | XSAVE状态转储实现 |

#### 6.2 寄存器Clobber定义 (cpu_device.h:19-107)

```c
#define RCLOBBEREDLIST     // R8-R15寄存器
#define MMCLOBBEREDLIST    // MM0-MM7寄存器
#define XMMCLOBBEREDLIST   // XMM0-XMM15寄存器
#define YMMCLOBBEREDLIST   // YMM0-YMM15寄存器
#define ZMMCLOBBEREDLIST   // ZMM0-ZMM31寄存器
```

---

## 七、总结

OpenDCDiag是一个高度针对x86架构的诊断工具，主要依赖以下x86特定技术：

1. **CPUID指令** - 用于检测CPU特性、拓扑结构和微code版本
2. **MSR访问** - 读取SMI计数、性能计数器和PPIN
3. **XSAVE/XRSTOR** - 管理AVX-512和AMX等扩展状态
4. **SIMD Intrinsics** - 使用MMX/SSE/AVX/AVX-512指令进行测试
5. **AMX指令** - Intel高级矩阵扩展的tile加载/存储操作
6. **KVM虚拟化** - 完整的x86虚拟机支持

代码库通过条件编译(`#ifdef __x86_64__`)实现跨平台支持，但核心诊断功能主要面向x86-64架构。
