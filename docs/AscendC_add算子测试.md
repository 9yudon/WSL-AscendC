# AscendC Day 2: VS Code 配置、构建修复与仿真测试执行

> 2026-07-02 | 前期环境: CANN 8.0.0 on WSL2 AscendUbuntu

---

## 一、今日任务总览

| 任务 | 结果 |
|------|------|
| VS Code Remote-WSL 开发环境配置 | 完成 |
| `kernel_operator.h` IntelliSense 报错修复 | 已解决 |
| 产物路径从 `/usr/local/` 迁移到项目内 | 已解决 |
| 仿真测试执行步骤与日志分析 | 已梳理 |
| GitHub 仓库推送 | 已推送至 `9yudon/WSL-AscendC` |

---

## 二、VS Code + WSL2 开发环境配置

### 2.1 原理

VS Code 的 Remote-WSL 扩展在 WSL 内部启动一个 VS Code Server，所有文件读写、终端、编译都在 Linux 环境中进行。UI 渲染在 Windows 侧，代码执行在 WSL 侧，无需文件同步。

### 2.2 配置清单

安装了三个 VS Code 扩展:

```
Remote - WSL      (ms-vscode-remote.remote-wsl)
C/C++             (ms-vscode.cpptools)
CMake Tools       (ms-vscode.cmake-tools)
```

创建了三个项目级配置文件:

#### `.vscode/c_cpp_properties.json` — IntelliSense 配置

```json
{
  "compileCommands": "${workspaceFolder}/build/compile_commands.json",
  "includePath": [
    "${workspaceFolder}/include",
    "…/ascendc/include/basic_api",
    "…/ascendc/include/basic_api/impl",      // ★ 缺了这个会报错
    "…/ascendc/include/basic_api/interface", // ★ 缺了这个会报错
    "…/tikcpp/tikcfw",
    "…/tikcpp/tikcfw/interface",
    "…/tikcpp/tikcfw/impl"
  ],
  "defines": ["__aicore__", "__CCE_AICORE__=200"]
}
```

#### `.vscode/tasks.json` — 构建任务

- `Ctrl+Shift+B` → 一键构建 (cmake configure + make)
- `Ctrl+Shift+P → Tasks: Run Task → sim test` → 仅仿真测试
- `Ctrl+Shift+P → Tasks: Run Task → configure + build + test` → 全流程

#### `.vscode/settings.json` — 编辑器设置

```
C_Cpp.default.compileCommands → build/compile_commands.json
files.associations → *.h 视为 C++
```

---

## 三、IntelliSense `kernel_operator.h` 报错

### 3.1 现象

VS Code 中 `src/vector_add_kernel.cpp` 第 14 行:

```cpp
#include "kernel_operator.h"  // ← 红色波浪线
```

但 CMake 构建完全正常，`make` 无任何编译错误。

### 3.2 排查

**Step 1**: 确认头文件存在。

```bash
$ find /usr/local/Ascend -name "kernel_operator.h" -type f
/usr/local/Ascend/.../x86_64-linux/ascendc/include/basic_api/kernel_operator.h
/usr/local/Ascend/.../tools/tikcpp/tikcfw/kernel_operator.h
```

文件存在，不是路径问题。

**Step 2**: 查看 `kernel_operator.h` 内部的 `#include` 链。

```cpp
// kernel_operator.h 内部:
#include "kernel_tpipe_impl.h"
#include "kernel_tensor_impl.h"
#include "kernel_type.h"
#include "kernel_operator_intf.h"
#include "inner_interface/inner_kernel_operator_intf.h"
```

**Step 3**: 查找这些被 include 的文件的实际位置。

```bash
$ find /usr/local/Ascend -name "kernel_tpipe_impl.h" -type f
/usr/local/Ascend/.../ascendc/include/basic_api/impl/kernel_tpipe_impl.h

$ find /usr/local/Ascend -name "kernel_type.h" -type f
/usr/local/Ascend/.../ascendc/include/basic_api/interface/kernel_type.h
```

**根因**: 文件在 `basic_api/impl/` 和 `basic_api/interface/` 子目录下，但 `c_cpp_properties.json` 的 `includePath` 没有包含这两个子目录。

编译通过是因为 `ccec` 编译器有这些路径的内置配置。VS Code IntelliSense 只能靠 `includePath` 和 `compile_commands.json`。

### 3.3 解决

1. 在 `c_cpp_properties.json` 的 `includePath` 中加入:
   ```
   ".../ascendc/include/basic_api/impl"
   ".../ascendc/include/basic_api/interface"
   ```
2. 添加 `"compileCommands": "${workspaceFolder}/build/compile_commands.json"`
3. CMake 构建时加 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
4. VS Code 中 `Ctrl+Shift+P → C/C++: Reload IntelliSense Configuration`

### 3.4 教训

嵌套 `#include` 链解析失败时，不能只看表面的头文件是否存在，要逐级追踪每个 `#include` 的实际文件位置，补全所有子目录路径。

---

## 四、构建产物路径问题

### 4.1 现象

```bash
$ make
ld.lld: error: cannot open output file
  /usr/local/single_objects/vector_add_kernel/vector_add_kernel.o:
  Permission denied
```

### 4.2 排查

**Step 1**: 定位路径来源。

`/usr/local/single_objects/` 是什么？搜索 CANN CMake 源码:

```cmake
# function.cmake 中的 ascendc_library():
set(${device_target}_obj_install_dir
    ${CMAKE_INSTALL_PREFIX}/single_objects/${target_name})
```

**根因**: `CMAKE_INSTALL_PREFIX` 默认值是 `/usr/local`，和 CANN SDK 安装目录重合。`ascendc_library()` 把构建产物写进 SDK 目录，第一次 `sudo` 构建后文件属于 root，后续普通用户 `make` 无法覆盖。

**设计问题**: 构建产物不应和 SDK 混放。SDK 文件是只读的，开发产物应留在项目目录。

### 4.3 解决

在 `CMakeLists.txt` 的 `include(ascendc.cmake)` **之前**重设安装前缀:

```cmake
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/out" CACHE PATH
        "Install root — build outputs stay inside the project" FORCE)
endif()
```

修复后产物路径:

```
/usr/local/single_objects/...          ← 修复前 (需 sudo)
/mnt/d/AscendC/build/out/single_objects/...  ← 修复后 (项目内)
```

### 4.4 教训

使用第三方 CMake 框架时，先查框架内部对 `CMAKE_INSTALL_PREFIX` 的依赖，必要时在 `include()` 之前重设，避免默认路径污染系统目录。

---

## 五、仿真测试执行步骤

### 5.1 一键运行

```bash
wsl -d AscendUbuntu -e bash -c '
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export ASCEND_CANN_PACKAGE_PATH="${ASCEND_TOOLKIT_HOME}"
export SIMULATOR_PATH="${ASCEND_TOOLKIT_HOME}/x86_64-linux/simulator"
export LD_LIBRARY_PATH="${SIMULATOR_PATH}/Ascend310P1/lib:${ASCEND_TOOLKIT_HOME}/x86_64-linux/lib64:..."

cd /mnt/d/AscendC
mkdir -p build && cd build
cmake .. -DSOC_VERSION=ascend310p3 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..
python3 test/run_sim_test.py
'
```

### 5.2 测试执行的 8 个步骤

| 步骤 | 操作 | 对应日志 |
|------|------|----------|
| **1** | 编译 kernel | `[compile] Using CMake-built kernel ... size = 2416 bytes` |
| **2** | 准备测试数据 | A=[0,1,2…], B=[0,2,4…], Tiling(N=256, B=256) |
| **3** | 加载仿真器 .so | `Load ascend simulator success.` |
| **4** | 虚拟设备初始化 | `rtSetDevice() success.`, `rtStreamCreate() success.` |
| **5** | 分配显存 + 数据搬入 | `rtMalloc() ×4`, `rtMemcpy() ×3`, `rtMemset() ×1` |
| **6** | 注册 + 启动 kernel | `rtDevBinaryRegister()`, `rtFunctionRegister()`, `rtKernelLaunch()` |
| **7** | 同步 + 结果搬出 | `rtStreamSynchronize()`, `rtMemcpy() ×2` |
| **8** | 比对验证 + 清理 | `[PASS]`, `rtFree() ×4`, `rtDeviceReset()` |

---

## 六、执行日志分析

### 6.1 完整回显 (精简版)

```
============================================================
  AscendC VectorAdd Simulator Test (WSL2)
============================================================
  SOC:        Ascend310P1           ← 目标架构 (AI Core m200)
  Sim mode:   pv                    ← Programmer's View 仿真
  Kernel:     .../vector_add_kernel.cpp

[compile] Using CMake-built kernel ... (2416 bytes)

[INFO] Load RTS shared library...           ← 加载 AscendCL 运行时
[INFO] start load ascend simulator         ← 开始加载仿真器
[INFO] Load ascend simulator success.       ← 仿真器就绪
[INFO] rtSetDevice() success.              ← 选择 device 0
[INFO] rtStreamCreate() success.           ← 创建执行流
[INFO] rtMalloc() success. (×4)            ← 分配 A,B,C,Tiling
[INFO] rtMemcpy() success. (×3)            ← 数据搬入 H2D
[INFO] rtMemset() success.                 ← 输出 C 清零
[INFO] rtDevBinaryRegister() success.      ← 注册 kernel 二进制
[INFO] rtFunctionRegister() success.       ← 获取 kernel 入口
[INFO] rtKernelLaunch() success.           ← ★ 启动 kernel!
[INFO] rtStreamSynchronize() success.      ← 等待执行完成
[INFO] rtMemcpy() success. (×2)            ← 结果搬出 D2H

[result] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[expect] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[result] max diff = 0.000000e+00

[PASS]  VectorAdd simulation test passed!

[INFO] rtFree() success. (×4)              ← 释放显存
[INFO] rtStreamDestroy() success.          ← 销毁执行流
[INFO] rtDeviceReset() success.            ← 重置设备
```

### 6.2 关键阶段分析

**阶段 A: 仿真器初始化** (约 3 秒)

```
Load RTS shared library...
start load ascend simulator
Load ascend simulator success.
```

仿真器在此阶段:
1. 加载 9 个 .so 文件 (libc_sec → … → libruntime_cmodel)
2. 创建 24 个虚拟 AI Core (Ascend310P1 架构)
3. 初始化 DaVinci PEM 模型

非错误提示 `[ERROR] Config file [config_pv_aicore_model.toml] cannot be found.` 不影响执行，仿真器会使用内置默认配置。

**阶段 B: 数据准备与 kernel 加载** (约 3 秒)

```
rtMalloc()  ×4    → 分配 4 块 virtual HBM
rtMemcpy()  ×3    → Host → Device 数据传输
rtMemset()  ×1    → 输出缓冲区清零
rtDevBinaryRegister()  → 将 .o 加载为设备二进制
rtFunctionRegister()   → 查找 kernel 函数入口
```

**阶段 C: kernel 执行** (< 0.1 秒)

```
rtKernelLaunch() → AI Core 0 开始执行
```

仿真器在 CPU 上逐条解释执行 AI Core 指令:
- `DataCopy` → 仿真 GM↔UB 数据搬移
- `Add` → 调用 VecBinary 单元
- `EnQue`/`DeQue` → Pipe 同步

**阶段 D: 结果验证**

```
max diff = 0.000000e+00  → 完全一致
```

硬件级仿真保证位精确结果，在真实 NPU 上也会得到相同输出。

---

## 七、问题清单

| # | 问题 | 根因 | 解决 |
|---|------|------|------|
| 1 | `kernel_operator.h` IntelliSense 报错 | includePath 缺 `basic_api/impl/` 和 `basic_api/interface/` | 补充子目录路径 + compile_commands.json |
| 2 | `make` 报 Permission denied | 产物默认写到 `/usr/local/single_objects/`，与 SDK 目录冲突 | `CMAKE_INSTALL_PREFIX` 设为 `build/out` |
| 3 | `su` 报 Authentication failure | WSL 默认无 root 密码 | `wsl -d AscendUbuntu -u root` |
| 4 | GitHub OAuth 超时 | 网络限制，`login/device/code` 不可达 | 生成 SSH key，手动添加到 GitHub |
