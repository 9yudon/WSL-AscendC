# AscendC 算子开发环境搭建与仿真测试 — 实验报告

> 日期: 2026-07-01  
> 环境: Windows 11 + WSL2 (Ubuntu 22.04) + CANN 8.0.0  
> 目标: 在无 NPU 硬件的 WSL2 中搭建 AscendC 算子开发与 CPU 仿真测试环境

---

## 1. 实验背景

华为昇腾 CANN (Compute Architecture for Neural Networks) 是 Ascend NPU 的软件栈。AscendC 是 CANN 中的 C++ 算子开发框架。**CANN 没有 Windows 原生版本**，但 WSL2 中可以安装 Linux 版 CANN，并利用内置的 CPU 仿真器完成算子的编译、运行和验证。

## 2. 环境准备

### 2.1 前置条件

| 组件 | 版本 | 说明 |
|------|------|------|
| Windows | 11 Pro | WSL2 支持 |
| WSL 发行版 | Ubuntu 22.04 (`AscendUbuntu`) | x86_64, 15GB RAM |
| CMake | 3.22 | `apt install cmake` |
| g++ | 11.4 | `apt install build-essential` |
| Python | 3.10 | `apt install python3 python3-pip` |

### 2.2 CANN 安装

CANN 8.0.0 社区版下载自华为云 OBS:

```bash
# 下载 (约 2GB)
wget https://ascend-repo.obs.cn-east-2.myhuaweicloud.com/CANN/CANN%208.0.0/Ascend-cann-toolkit_8.0.0_linux-x86_64.run

# 安装
chmod +x Ascend-cann-toolkit_8.0.0_linux-x86_64.run
sudo ./Ascend-cann-toolkit_8.0.0_linux-x86_64.run --full --quiet
```

安装后路径: `/usr/local/Ascend/ascend-toolkit/8.0.0/`，软链接 `latest` → `8.0.0`。

**CANN 没有 Windows 版**。Windows 上的替代方案只有 WSL2 或 MindStudio 远程连接 Linux 服务器。

### 2.3 环境变量

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export ASCEND_CANN_PACKAGE_PATH="${ASCEND_TOOLKIT_HOME}"
export SOC_VERSION=ascend310p3
export SIMULATOR_PATH="${ASCEND_TOOLKIT_HOME}/x86_64-linux/simulator"
export LD_LIBRARY_PATH="${SIMULATOR_PATH}/Ascend310P1/lib:${ASCEND_TOOLKIT_HOME}/x86_64-linux/lib64:..."
```

关键变量说明:
- `ASCEND_TOOLKIT_HOME` — CANN 安装根路径
- `ASCEND_CANN_PACKAGE_PATH` — CMake 构建必需，指向 `${ASCEND_TOOLKIT_HOME}`
- `SOC_VERSION` — 目标 NPU 架构 (`ascend310p3` → AI Core m200)
- `SIMULATOR_PATH` — 仿真器库路径

## 3. 项目结构

```
AscendC/
├── CMakeLists.txt                 # 顶层构建 (CANN 工具链探测 + ascendc.cmake)
├── cmake/FindCANN.cmake           # CANN 安装路径自动探测 (备选)
├── include/vector_add_kernel.h    # Tiling 数据结构 (host/device 共享)
├── src/
│   ├── CMakeLists.txt             # ascendc_library() 编译规则
│   └── vector_add_kernel.cpp      # Kernel 实现 (AI Core 目标码)
├── test/
│   ├── run_sim_test.py            # CPU 仿真测试 (Python, 基于 op_test_frame)
│   └── test_vector_add.cpp        # Host 侧测试模板 (需 NPU 硬件)
├── scripts/env.sh                 # 一键环境配置
├── docs/                          # 实验报告
└── README.md
```

## 4. CMake 构建系统

### 4.1 顶层 CMakeLists.txt 核心要点

```cmake
# 1. 设置 ASCEND_CANN_PACKAGE_PATH (CMake 变量)
set(ASCEND_CANN_PACKAGE_PATH "${ASCEND_TOOLKIT_HOME}")

# 2. 设置 ASCENDC_KERNEL_CMAKE_DIR
set(ASCENDC_KERNEL_CMAKE_DIR "${ASCEND_TOOLKIT_HOME}/x86_64-linux/tikcpp/ascendc_kernel_cmake")

# 3. 必须设置 CMAKE_BUILD_TYPE (merge_device_obj.py 需要)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# 4. 加载 CANN CMake 模块
include(${ASCENDC_KERNEL_CMAKE_DIR}/ascendc.cmake)
```

### 4.2 Kernel 编译规则

```cmake
# ascendc_library() 是 CANN 提供的 CMake 函数:
#   1. 解析 kernel 源码
#   2. 生成 auto-gen wrapper (处理参数序列化)
#   3. 调用 Bisheng (ccec) 交叉编译器生成 AI Core 目标码
#   4. 链接为 .o 文件
ascendc_library(vector_add_kernel OBJECT
    vector_add_kernel.cpp
)
```

产物: `/usr/local/single_objects/vector_add_kernel/vector_add_kernel.o`

### 4.3 构建命令

```bash
mkdir -p build && cd build
cmake .. -DSOC_VERSION=ascend310p3 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 5. VectorAdd Kernel 实现

### 5.1 Tiling 数据结构

```cpp
// include/vector_add_kernel.h
struct VectorAddTilingData {
    uint32_t totalLength; // 总元素个数
    uint32_t blockDim;    // 每次循环处理的元素数 (≤256)
};
```

### 5.2 Kernel 函数签名

```cpp
extern "C" __global__ __aicore__ void vector_add_kernel(
    GM_ADDR a,       // input  — GM 上的 float 数组
    GM_ADDR b,       // input  — GM 上的 float 数组
    GM_ADDR c,       // output — GM 上的 float 数组
    GM_ADDR tiling); // tiling 数据
```

**关键规则**:
- Kernel 声明**只能放在 .cpp 文件**中，不能放头文件（否则 auto-gen wrapper 解析会出错）
- 所有指针参数必须用 `GM_ADDR` 类型（等效 `__gm__ void*`）
- 参数个数必须与框架传参匹配: `N_inputs + N_outputs + has_tiling`

### 5.3 数据流

```
Global Memory (GM)                    Unified Buffer (UB)
   ┌──────────┐                       ┌──────────────┐
   │  A[0..N] │ ── DataCopy ────────→ │   tA (float) │
   └──────────┘                       └──────────────┘
   ┌──────────┐                       ┌──────────────┐
   │  B[0..N] │ ── DataCopy ────────→ │   tB (float) │
   └──────────┘                       └──────────────┘
                                            │
                                     Add(tC, tA, tB)
                                            ↓
   ┌──────────┐                       ┌──────────────┐
   │  C[0..N] │ ←── DataCopy ──────── │   tC (float) │
   └──────────┘                       └──────────────┘
```

### 5.4 Pipe 同步模型

AscendC 中的 `TPipe` + `TBuf<>` + `EnQue`/`DeQue` 实现了异步操作的流水线同步:

```cpp
// 1. 从 buffer 分配 tensor
LocalTensor<float> t = buf.AllocTensor<float>();

// 2. 发起异步拷贝 (GM → UB)
DataCopy(t, gm[offset], n);

// 3. 入队 (信号: 拷贝请求已发出)
buf.EnQue(t);

// 4. 出队 (阻塞等待拷贝完成)
t = buf.DeQue<float>();

// 5. 同步计算 (或再次 DataCopy 写回 GM)
Add(tResult, t, tOther, n);  // Add 也是异步的, 需要 EnQue/DeQue
```

**规则**: 每个 `DataCopy` 或 `Add` 之后都需要 `EnQue/DeQue` 对来同步。

### 5.5 分块 (Tiling) 处理

```cpp
uint32_t tileCount = (totalLength + blockDim - 1) / blockDim;
for (uint32_t i = 0; i < tileCount; ++i) {
    uint32_t offset = i * blockDim;
    uint32_t thisBlock = (i == tileCount - 1) ? (totalLength - offset) : blockDim;
    // ... process thisBlock elements starting at offset
}
```

最后一个块可能不足 `blockDim` 个元素，需要特殊处理。

## 6. CPU 仿真测试

### 6.1 仿真器原理

CANN 自带基于 C++ 共享库的 CPU 仿真器，位于:

```
/usr/local/Ascend/ascend-toolkit/latest/x86_64-linux/simulator/
├── Ascend310P1/          ← AI Core (m200), 纯 AI Core 架构
│   └── lib/
│       ├── libpem_davinci.so
│       ├── libnpu_drv_pvmodel.so
│       ├── libstars_pv.so
│       ├── libmodel_top_pv.so
│       └── libruntime_cmodel.so
├── Ascend910B1/          ← 混合 AIC+AIV 架构
└── ...
```

仿真器替换了真实的 NPU 驱动和运行时。Host 程序调用 `aclrtKernelLaunch` 时，不会发送到物理 NPU，而是在 CPU 上模拟执行 AI Core 指令。

### 6.2 仿真器加载顺序

```
1. libc_sec.so          ← CANN 基础依赖
2. libmmpa.so           ← 内存管理
3. libascendalog.so     ← 日志
4. libplatform.so       ← 平台抽象 (含 fe::PlatformInfoManager)
5. libpem_davinci.so    ← DaVinci 架构模拟
6. libnpu_drv_pvmodel.so
7. libstars_pv.so
8. libmodel_top_pv.so
9. libruntime_cmodel.so ← 运行时模型
```

**踩坑**: 仿真器 .so 依赖 CANN 基础库 (`libplatform.so` 等)，必须将它们加入 `LD_LIBRARY_PATH`，否则 `ctypes.CDLL` 会报 `undefined symbol`。

### 6.3 测试脚本架构

```python
# 1. 编译 (或使用 CMake 构建的 .o)
kernel_o = "/usr/local/single_objects/vector_add_kernel/vector_add_kernel.o"

# 2. 创建 kernel 对象 (自动解析函数名)
op_kernel = AscendOpKernel(kernel_o, ascendc_cfg={
    "block_dim": 1,
    "ascendc_op_path": KERNEL_CPP,
})

# 3. 设置输入/输出
op_kernel.set_input_info([
    {"shape": [N], "dtype": "float32", "param_type": "input"},
    {"shape": [N], "dtype": "float32", "param_type": "input"},
])
op_kernel.set_output_info([
    {"shape": [N], "dtype": "float32", "param_type": "output"},
])

# 4. 启动仿真器
with AscendOpKernelRunner(simulator_mode="pv", soc_version="Ascend310P1",
                          simulator_lib_path=SIMULATOR_PATH) as runner:
    output = runner.run(op_kernel,
                        inputs=[a_numpy, b_numpy],
                        tiling=struct.pack("<II", N, BLOCK_DIM))

    # 必须在 with 块内 sync，因为 __exit__ 会释放设备内存
    output.sync_from_device()
    result = output.get_data()
```

### 6.4 测试结果

```
[result] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[expect] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[result] max diff = 0.000000e+00
[PASS]  VectorAdd simulation test passed!
```

## 7. 踩坑记录

### 7.1 Kernel 参数个数不匹配

**现象**: 仿真器报 `scalar_pv.cc:1328 memory_map_calc Scalar: invalid addr`、`div by 0!`、输出全零

**原因**: Kernel 有 5 个参数 `(a, b, c, workspace, tiling)`，但框架只传 4 个 `(inputs[0], inputs[1], outputs[0], tiling)`。workspace 参数不在框架的 `kernel_args` 中，导致 tiling 指针被当作 workspace 传给 kernel，而真正的 tiling 数据无法读取。

**解决**: 移除不用的 workspace 参数，使 kernel 参数个数与框架传参一致。

```diff
- void vector_add_kernel(GM_ADDR a, GM_ADDR b, GM_ADDR c, GM_ADDR workspace, GM_ADDR tiling)
+ void vector_add_kernel(GM_ADDR a, GM_ADDR b, GM_ADDR c, GM_ADDR tiling)
```

### 7.2 头文件中声明 kernel 函数导致 auto-gen 解析失败

**现象**: CMake 构建报 `expected unqualified-id`、`conflicting types`、`no matching function for call`

**原因**: CANN 的 auto-gen wrapper 使用 `#define` 重命名 kernel 函数，然后 `#include` 源文件。如果头文件中也声明了 kernel 函数，预处理器会在错误的上下文中展开。

**解决**: Kernel 函数**只在 .cpp 中声明和定义**，头文件只放数据结构。

```cpp
// ✅ 正确: vector_add_kernel.h 只包含数据结构
struct VectorAddTilingData { uint32_t totalLength; uint32_t blockDim; };

// ✅ 正确: vector_add_kernel.cpp 包含 kernel 声明
extern "C" __global__ __aicore__ void vector_add_kernel(GM_ADDR a, GM_ADDR b, ...);
```

### 7.3 CMAKE_BUILD_TYPE 未设置导致链接失败

**现象**: `merge_device_obj.py: error: argument --build-type: expected one argument`

**原因**: Unix Makefiles 生成器默认 `CMAKE_BUILD_TYPE` 为空，而 CANN 的 `merge_device_obj.py` 脚本需要显式的构建类型。

**解决**: 在 CMakeLists.txt 中设置默认值或在命令行指定 `-DCMAKE_BUILD_TYPE=Release`。

### 7.4 仿真器 .so 加载顺序

**现象**: `OSError: libstars_pv.so: cannot open shared object file`

**原因**: 仿真器 .so 之间有依赖关系，Python ctypes 加载时必须按依赖顺序加载。必须先加载 CANN 基础库 (`libc_sec.so`, `libmmpa.so`, `libascendalog.so`, `libplatform.so`)，再加载仿真器 .so。

**解决**: 设置 `LD_LIBRARY_PATH` 包含所有 .so 目录，或在 Python 中用 `ctypes.CDLL` 预加载依赖。

### 7.5 Context Manager 退出后数据丢失

**现象**: `get_data()` 返回 `None`

**原因**: `AscendOpKernelRunner.__exit__()` 调用 `device_reset()` 释放所有设备内存。如果在 with 块外部调用 `sync_from_device()`，HBM 指针已经无效。

**解决**: 在 with 块内部调用 `sync_from_device()` + `get_data()`。

```python
with AscendOpKernelRunner(...) as runner:
    output = runner.run(...)
    output.sync_from_device()  # ← 必须在 with 块内
    result = output.get_data()  # ← 必须在 with 块内
```

### 7.6 SOC_VERSION 选择

**问题**: 最初选择 `ascend910b` (混合 AIC+AIV 架构)，仿真器有复杂的配置依赖。

**解决**: 改用 `ascend310p3` (纯 AI Core m200 架构)，仿真器设置更简单，且 `op_test_frame` 对此 SoC 支持更好。

| SOC_VERSION | 架构 | 仿真器状态 |
|---|---|---|
| `ascend310p3` | AI Core (m200) | ✅ 正常工作 |
| `ascend910b` | AIC+AIV 混合 | ⚠️ 需额外配置 |

## 8. 开发工作流总结

### 新建一个 AscendC 算子的标准步骤

```
1. 在 include/ 定义 tiling 结构体

2. 在 src/ 编写 kernel 实现:
   - #include "kernel_operator.h"
   - using namespace AscendC;
   - extern "C" __global__ __aicore__ void kernel_name(GM_ADDR ...)
   - 用 TPipe + TBuf<> + DataCopy + Compute + EnQue/DeQue 模式

3. 在 src/CMakeLists.txt 添加:
   ascendc_library(kernel_name OBJECT kernel_name.cpp)

4. 构建:
   mkdir -p build && cd build
   cmake .. -DSOC_VERSION=ascend310p3 -DCMAKE_BUILD_TYPE=Release
   make -j$(nproc)

5. 仿真测试:
   python3 test/run_sim_test.py

6. 真实硬件部署:
   将 .o 部署到 Ascend NPU 服务器, 通过 AscendCL 调用
```

### 一键命令

```bash
wsl -d AscendUbuntu
cd /mnt/d/AscendC
source scripts/env.sh
mkdir -p build && cd build && cmake .. && make -j$(nproc) && cd ..
python3 test/run_sim_test.py
```

踩坑记录包含本次实验排查的 6 个核心问题：
  1. Kernel 参数个数与框架不匹配 → workspace 导致参数错位
  2. 头文件声明 kernel 函数 → auto-gen wrapper 解析失败
  3. CMAKE_BUILD_TYPE 未设置 → 链接步骤崩溃
  4. 仿真器 .so 加载顺序 → libplatform.so 等依赖缺失
  5. Context manager 退出后数据丢失 → 必须在 with 块内 sync
  6. SOC_VERSION 选择 → ascend310p3 纯 AI Core 比 ascend910b 混合架构更适合仿真

## 9. 参考资料

- CANN 社区版下载: https://www.hiascend.com/software/cann/community-history
- CANN 安装文档: https://www.hiascend.com/document/detail/zh/canncommercial/800/softwareinst/instg/instg_0000.html
- AscendC 开发指南: https://www.hiascend.com/document/detail/zh/canncommercial/800/devguide/opsref/opsref_0001.html
- OBS 下载源: https://ascend-repo.obs.cn-east-2.myhuaweicloud.com/
