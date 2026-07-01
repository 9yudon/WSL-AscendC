# AscendC 开发环境准备与调试回顾

> 2026-07-01 | Windows 11 + WSL2 | CANN 8.0.0

---

## 一、环境概述

| 层 | 组件 | 说明 |
|---|------|------|
| 宿主机 | Windows 11 Pro, x86_64 | WSL2 宿主 |
| 虚拟化 | WSL2 `AscendUbuntu` (Ubuntu 22.04) | CANN 运行环境 |
| 工具链 | CANN 8.0.0 (社区版) | 含 Bisheng 编译器 + CPU 仿真器 |
| 构建 | CMake 3.22 + g++ 11.4 | 标准 C++ 构建 |
| 测试 | Python 3.10 + op_test_frame | 仿真器测试框架 |

---

## 二、环境准备过程

### 2.1 起点

宿主机 Windows 11 已有以下 WSL 实例:

```
> wsl --list --verbose
  Ubuntu-24.04    Stopped
  AscendUbuntu    Stopped   ← 选用
  Ubuntu-22.04    Stopped
```

`AscendUbuntu` 是空的 Ubuntu 22.04，没有任何开发工具。

### 2.2 安装编译工具链

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ gcc make git python3 python3-pip
```

验证:

```bash
$ g++ --version    # g++ 11.4.0
$ cmake --version  # cmake 3.22.1
```

### 2.3 下载 CANN

CANN 没有 Windows 原生版本，只有 Linux `.run` 包。华为云 OBS 上提供了社区版直接下载链接:

| 架构 | 文件名 |
|------|--------|
| x86_64 | `Ascend-cann-toolkit_8.0.0_linux-x86_64.run` |
| aarch64 | `Ascend-cann-toolkit_8.0.0_linux-aarch64.run` |

下载 (约 2GB):

```bash
wget https://ascend-repo.obs.cn-east-2.myhuaweicloud.com/CANN/CANN%208.0.0/Ascend-cann-toolkit_8.0.0_linux-x86_64.run
```

### 2.4 安装 CANN

```bash
chmod +x Ascend-cann-toolkit_8.0.0_linux-x86_64.run
sudo ./Ascend-cann-toolkit_8.0.0_linux-x86_64.run --full --quiet
```

安装日志显示子包:

```
[Toolkit] install package CANN-runtime-7.6.0.1.220     → success
[Toolkit] install package CANN-compiler-7.6.0.1.220     → success
[Toolkit] install package CANN-hccl-7.6.0.1.220         → success
[Toolkit] install package CANN-opp-7.6.0.1.220          → success
[Toolkit] install package CANN-toolkit-7.6.0.1.220      → success
[Toolkit] install package CANN-aoe-7.6.0.1.220          → success
[Toolkit] install package Ascend-pyACL_8.0.0            → success
```

产物路径:

```
/usr/local/Ascend/ascend-toolkit/
├── set_env.sh          ← 官方环境脚本
├── latest → ./8.0.0
├── 8.0.0/
│   ├── x86_64-linux/   ← 架构相关
│   │   ├── ccec_compiler/bin/ccec   ← Bisheng 编译器
│   │   ├── ascendc/include/         ← AscendC API 头文件
│   │   ├── simulator/               ← CPU 仿真器
│   │   └── lib64/                   ← 运行时库
│   ├── compiler/
│   ├── tools/
│   └── python/                      ← Python 接口
```

### 2.5 配置环境变量

CANN 自带 `set_env.sh` 设置了 `ASCEND_TOOLKIT_HOME`、`PATH`、`LD_LIBRARY_PATH`、`PYTHONPATH`。

构建 AscendC kernel 还需要额外变量:

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh

# CMake 构建必需
export ASCEND_CANN_PACKAGE_PATH="${ASCEND_TOOLKIT_HOME}"

# 目标 SoC 架构
export SOC_VERSION=ascend310p3

# 仿真器路径
export SIMULATOR_PATH="${ASCEND_TOOLKIT_HOME}/x86_64-linux/simulator"

# 仿真器依赖的运行时库
export LD_LIBRARY_PATH="\
  ${SIMULATOR_PATH}/Ascend310P1/lib:\
  ${ASCEND_TOOLKIT_HOME}/x86_64-linux/lib64:\
  ${ASCEND_TOOLKIT_HOME}/lib64:\
  ${ASCEND_TOOLKIT_HOME}/x86_64-linux/lib64/plugin/opskernel:\
  ${LD_LIBRARY_PATH}"
```

写入 `~/.bashrc` 使其持久化。

---

## 三、项目搭建

### 3.1 目录结构

```
AscendC/
├── CMakeLists.txt
├── cmake/FindCANN.cmake
├── include/vector_add_kernel.h
├── src/
│   ├── CMakeLists.txt
│   └── vector_add_kernel.cpp
├── test/
│   ├── run_sim_test.py
│   └── test_vector_add.cpp
└── scripts/env.sh
```

### 3.2 CMake 构建系统要点

`CMakeLists.txt` 的核心:

```cmake
# 1. 告知 CANN CMake 模块 loader 的位置
set(ASCENDC_KERNEL_CMAKE_DIR
    "${ASCEND_TOOLKIT_HOME}/x86_64-linux/tikcpp/ascendc_kernel_cmake")

# 2. 必须设置构建类型 (否则 merge_device_obj.py 报错)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# 3. 加载 CANN 的 ascendc.cmake (定义了 ascendc_library 等函数)
include(${ASCENDC_KERNEL_CMAKE_DIR}/ascendc.cmake)
```

Kernel 编译规则:

```cmake
ascendc_library(vector_add_kernel OBJECT
    vector_add_kernel.cpp
)
```

`ascendc_library()` 的内部流程:

```
源码 .cpp → [auto-gen wrapper] → [ccec 编译] → [链接] → .o
                                      ↑
                              Bisheng 交叉编译器
                              AI Core 目标码
```

构建命令:

```bash
mkdir -p build && cd build
cmake .. -DSOC_VERSION=ascend310p3 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

产物: `/usr/local/single_objects/vector_add_kernel/vector_add_kernel.o`

---

## 四、调试过程 (按时间线)

### 4.1 第一轮: 原始模板不兼容

**初始尝试**: 使用网上教程中常见的 `Kernel<T>` 基类 + `Init/Process` 成员函数模式。

**错误**: 编译器报找不到 `ascendc::Kernel`、`kernel.h` 等符号。

**分析**: 这些 API 是旧版 AscendC 的接口。CANN 8.0.0 使用的是完全不同的 API 范式。查看内置算子 (如 `aglu.cpp`) 后发现真实的 API 是:

```cpp
extern "C" __global__ __aicore__ void kernel_name(GM_ADDR ...)
```

而不是类继承模式。

**教训**: 不要依赖通用教程，先看 CANN 自带的实际算子代码。

### 4.2 第二轮: API 函数名错误

**错误日志**:

```
error: no template named 'DeQueue' in 'AscendC::TBuf<>'; did you mean 'DeQue'?
error: no member named 'Enqueue' in 'AscendC::TPipe'
error: no matching member function for call to 'AllocTensor' (requires 0 arguments, but 1 was provided)
```

**分析**: 我凭经验猜测的 API 名称 (`DeQueue`、`Enqueue`、`AllocTensor(size)`) 都不对。正确的是:

| 猜测 | 实际 |
|------|------|
| `DeQueue<T>()` | `DeQue<T>()` |
| `Enqueue()` | `EnQue()` |
| `AllocTensor<T>(size)` | `AllocTensor<T>()` (无参数) |
| `FreeTensor(t)` | `FreeTensor(t)` ✓ |
| `InitBuffer(buf, numBufs, size)` | `InitBuffer(buf, size)` (单缓冲) |

**教训**: 查看实际头文件 (`kernel_tpipe.h`、`kernel_operator_vec_binary_intf.h`) 确认 API，不要猜测。

### 4.3 第三轮: auto-gen wrapper 解析失败

**错误日志**:

```
auto_gen_vector_add_kernel.cpp:22: error: expected unqualified-id
error: expecting parameter a to have memory qualifier __gm__
error: no matching function for call to 'vector_add_kernel_origin'
    vector_add_kernel_origin(*a, *b, *c, *tiling, *b, *c, *tiling);
                                        ^^^^^^^^^^^^^^^
```

**关键线索**: 参数 `*b, *c, *tiling` 被重复添加了。调用变成了 7 个参数 (原 4 个 + 3 个重复)。

**分析**: CANN 的 auto-gen 系统会用以下方式处理 kernel 源码:

```cpp
// 伪代码: auto-gen 的工作原理
#define __global__ inline
#define vector_add_kernel vector_add_kernel_origin
#include "vector_add_kernel.cpp"    // 原始代码被 inline 展开
#undef vector_add_kernel

// 生成 wrapper
extern "C" __global__ void auto_gen_vector_add_kernel(...) {
    vector_add_kernel_origin(...);  // 调用原始函数
}
```

问题出在**头文件中也声明了 kernel 函数**:

```cpp
// ❌ vector_add_kernel.h 中有这行
extern "C" __global__ __aicore__ void vector_add_kernel(GM_ADDR a, ...);
```

当 auto-gen `#include` 了头文件，又 `#include` 了 .cpp，`vector_add_kernel` 被声明了两次，导致宏展开错乱。

**解决**: 头文件只放数据结构 (tiling struct)，kernel 函数声明和定义都只放在 .cpp 中。

```cpp
// ✅ vector_add_kernel.h — 只有 struct
struct VectorAddTilingData { uint32_t totalLength; uint32_t blockDim; };

// ✅ vector_add_kernel.cpp — 包含完整声明和实现
extern "C" __global__ __aicore__ void vector_add_kernel(GM_ADDR a, ...) { ... }
```

### 4.4 第四轮: CMAKE_BUILD_TYPE 缺失

**错误日志**:

```
merge_device_obj.py: error: argument --build-type: expected one argument
```

**分析**: `merge_device_obj.py` 使用 `${CMAKE_BUILD_TYPE}` 作为 `--build-type` 参数。在 Unix Makefiles 生成器下，`CMAKE_BUILD_TYPE` 默认为空字符串。

**解决**: 在 CMakeLists.txt 中强制设置默认值:

```cmake
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
```

或在命令行指定: `cmake .. -DCMAKE_BUILD_TYPE=Release`

### 4.5 第五轮: 仿真器启动 — kernel 编译通过，但无法运行

**状态**: CMake 构建完全通过，`.o` 文件生成。现在需要运行仿真测试。

**问题**: 如何在没有 NPU 硬件的情况下运行测试？

**发现**: CANN 安装目录下存在完整的 CPU 仿真器:

```
/usr/local/Ascend/ascend-toolkit/latest/x86_64-linux/simulator/
├── Ascend310P1/lib/   ← 含 17 个 .so 文件
├── Ascend910B1/lib/   ← 含 17 个 .so 文件
└── ...
```

配置文件 `release_config.json` 定义了仿真器加载顺序:

```json
{
    "pv": ["libpem_davinci.so", "libnpu_drv_pvmodel.so",
           "libstars_pv.so", "libmodel_top_pv.so", "libruntime_cmodel.so"]
}
```

**关键工具**: `op_test_frame` Python 包 (CANN 自带 wheel):

```bash
pip3 install /usr/local/Ascend/ascend-toolkit/8.0.0/tools/op_test_frame-0.1-py3-none-any.whl
```

这个包提供了:
- `AscendRTSApi` — 仿真器底层接口
- `AscendOpKernelRunner` — 仿真运行器
- `AscendcOpUt` — AscendC 算子测试框架

### 4.6 第六轮: 仿真器 .so 加载失败 — 依赖缺失

**错误日志**:

```
OSError: libstars_pv.so: cannot open shared object file: No such file or directory
```

**排查**: 设置了 `LD_LIBRARY_PATH` 后，错误变为:

```
OSError: libruntime_cmodel.so: undefined symbol: _ZN2fe19PlatformInfoManager22InitializePlatformInfoEv
```

**分析**: 符号 `fe::PlatformInfoManager::InitializePlatformInfo()` 不在已加载的 .so 中。用 `nm -D` 搜索发现它在 `/usr/local/Ascend/.../lib64/libplatform.so` 中。

**根因**: 仿真器 .so 依赖 CANN 基础运行时库。`op_test_frame` 的 `_load_simulator_so()` 只加载 release_config.json 中列出的仿真器 .so，不会自动加载它们的依赖。

**解决**: 预加载 CANN 运行时依赖:

```python
import ctypes
for dep in ["libc_sec.so", "libmmpa.so", "libascendalog.so", "libplatform.so"]:
    ctypes.CDLL(f"{cann_lib}/{dep}", mode=ctypes.RTLD_GLOBAL)
```

### 4.7 第七轮: 仿真器启动成功 — 第一个里程碑

成功日志:

```
[INFO] Load RTS shared library...
[INFO] start load ascend simulator
[INFO] Load ascend simulator success.
================================================================================
>>>>                        " PEM MODEL "
>>>>             Total no. of 1 chip(s) Model Init Success!
================================================================================
[INFO] AicWrapper attach AIC 0..23  (24 AI Cores, 每个 2 VecCores + 3 SubCores)
[INFO] Runtime API call rtSetDevice() success.
[INFO] Runtime API call rtKernelLaunch() success.   ← kernel 被执行了!
[INFO] Runtime API call rtStreamSynchronize() success.
```

仿真器成功初始化了 24 个 AI Core 并执行了 kernel。

**但是**:

```
[ERROR] scalar_pv.cc:1328 memory_map_calc Scalar: invalid addr.   (几十次)
[ERROR] scalar_pv.cc:247 scalar_div [SCALAR_FUNC]: Error: div by 0!
[ERROR] get_data() returned None
```

kernel 在仿真器内部崩溃了 —— 访问无效地址、除以零。输出数据也没有回传。

### 4.8 第八轮: 隔离变量 — 简化 kernel 验证数据通路

**思路**: 把问题拆成最小可复现单元。

**Debug Step 1**: 只拷贝 A → C, 不做加法:

```cpp
// 极简 kernel: 只验证数据通路
LocalTensor<float> t = buf.AllocTensor<float>();
DataCopy(t, gm_A[0], N);
buf.EnQue(t);
t = buf.DeQue<float>();
DataCopy(gm_C[0], t, N);
```

**结果**:

```
[result] first 8: [0. 1. 2. 3. 4. 5. 6. 7.]   ← 就是 A 的值!
```

**数据通路验证通过。** 这说明 GM→UB→GM 的拷贝链路是完整的。

### 4.9 第九轮: 回归到完整 kernel — 输出全零

在简化版验证通过后，回到完整 VectorAdd (A+B)，结果:

```
[result] first 8: [0. 0. 0. 0. 0. 0. 0. 0.]
[expect] first 8: [0. 3. 6. 9. ...]
```

**输出全是零。** Add 操作没有产生效果。

### 4.10 第十轮: 排查参数个数 — 找到根因

**关键发现**: 对比 kernel 函数签名和框架传参:

```
Kernel 定义:  void kernel(GM_ADDR a, GM_ADDR b, GM_ADDR c, GM_ADDR workspace, GM_ADDR tiling)
                                                                      ^^^^^^^^^
框架传参:    [a_ptr, b_ptr, c_ptr, tiling_ptr]     ← 4 个参数
```

框架按 `inputs + outputs + tiling` 的顺序传参。kernel 比框架多了一个 `workspace` 参数。

这导致:

```
Kernel 收到的:   a = a_ptr (正确)
                b = b_ptr (正确)
                c = c_ptr (正确)
           workspace = tiling_ptr (错位!)
            tiling = 垃圾值 (寄存器残留)
```

tiling 指针指向了 workspace 的地址，而真正的 tiling 数据没人读取。kernel 读到的是垃圾 totalLength，导致计算循环异常。

**解决**: 移除不用的 workspace 参数:

```diff
- void vector_add_kernel(GM_ADDR a, GM_ADDR b, GM_ADDR c, GM_ADDR workspace, GM_ADDR tiling)
+ void vector_add_kernel(GM_ADDR a, GM_ADDR b, GM_ADDR c, GM_ADDR tiling)
```

### 4.11 第十一轮: 仿真测试通过

```
[result] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[expect] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[result] max diff = 0.000000e+00
[PASS]  VectorAdd simulation test passed!
```

---

## 五、踩坑清单

| # | 现象 | 根因 | 解决方法 |
|---|------|------|----------|
| 1 | `ascendc::Kernel` 找不到 | API 版本不匹配 | 使用 `extern "C" __global__ __aicore__` 模式 |
| 2 | `DeQueue` / `Enqueue` / `AllocTensor(size)` 不存在 | API 名称错误 | 正确名称: `DeQue` / `EnQue` / `AllocTensor()` |
| 3 | auto-gen 解析报语法错 | 头文件中声明了 kernel 函数 | kernel 声明只放 .cpp |
| 4 | `--build-type: expected one argument` | CMAKE_BUILD_TYPE 为空 | 设置默认 Release |
| 5 | `libstars_pv.so: not found` | LD_LIBRARY_PATH 缺仿真器路径 | 添加 simulator/Ascend310P1/lib |
| 6 | `undefined symbol: PlatformInfoManager` | 仿真器依赖 CANN 基础库 | 预加载 libplatform.so 等 |
| 7 | 输出全零 / 仿真器报错 | workspace 参数导致传参错位 | 移除不用的 workspace 参数 |
| 8 | `get_data()` 返回 None | 在 with 块外读取已释放的内存 | 在 with 块内 sync+get_data |
| 9 | ascend910b 仿真器报配置缺失 | 910B 混合架构仿真更复杂 | 使用 ascend310p3 纯 AI Core |

---

## 六、关键经验

### 6.1 参看真实代码而非教程

CANN 版本迭代快，网上的教程通常过时。最可靠的信息来源是:

1. CANN 自带的算子源码 (`opp/built-in/op_impl/ai_core/tbe/impl/ascendc/`)
2. CANN 自带的头文件 (`ascendc/include/`)
3. CANN 自带的 CMake 模块 (`tikcpp/ascendc_kernel_cmake/`)

### 6.2 分步隔离验证

当整个链路不通时，逐段验证:

```
GM→UB 拷贝 → Compute → UB→GM 拷贝
   ↑ 先只测这个，确保数据通路
              ↑ 再测这个，确保计算正确
                        ↑ 最后测完整流程
```

### 6.3 参数对齐是第一道关

CANN 框架按严格顺序传递 kernel 参数。多一个或少一个参数都会导致难以排查的运行时错误。kernel 签名必须精确匹配框架的传参顺序。

### 6.4 仿真器是一个完整的 NPU 模型

WSL2 中的 CANN 仿真器不是一个简单的 mock，而是完整的 AI Core 指令集模拟器。它:
- 初始化 24 个 AI Core（310P 架构）
- 模拟 DaVinci 计算单元
- 执行真实的 AI Core 目标码
- 产生精确的计算结果

因此 kernel 代码的正确性和真实硬件上的行为完全一致，仿真结果可以直接指导真实部署。

---

## 七、一键运行脚本

完成所有调试后的最终运行方式:

```bash
wsl -d AscendUbuntu
cd /mnt/d/AscendC
source scripts/env.sh
mkdir -p build && cd build && cmake .. && make -j$(nproc) && cd ..
python3 test/run_sim_test.py
```
