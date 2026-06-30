# AscendC 算子开发工作区

基于华为昇腾 (Ascend) CANN 8.0.0 的 **AscendC 自定义算子开发项目**。

## 当前状态

| 项 | 状态 |
|---|------|
| CANN 8.0.0 | 已安装 (WSL2 `AscendUbuntu`) |
| ccec / Bisheng | 就绪 |
| CMake + g++ | 就绪 |
| VectorAdd kernel | 编译通过 + **仿真验证通过** |
| 仿真器 | WSL2 CPU 仿真 (Ascend310P1, PV 模式) |

## 快速开始

### 1. 进入 WSL 环境

```powershell
# Windows terminal / PowerShell
wsl -d AscendUbuntu
```

### 2. 加载环境

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export ASCEND_CANN_PACKAGE_PATH="${ASCEND_TOOLKIT_HOME}"
```

或一键加载项目脚本:

```bash
cd /mnt/d/AscendC
source scripts/env.sh
```

### 3. 构建 + 仿真测试

```bash
source scripts/env.sh
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行 CPU 仿真测试 (无需 NPU 硬件)
cd .. && python3 test/run_sim_test.py
```

期望输出:
```
[result] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[expect] first 8: [ 0.  3.  6.  9. 12. 15. 18. 21.]
[result] max diff = 0.000000e+00
[PASS]  VectorAdd simulation test passed!
```

### 4. 添加新算子

1. 在 `include/` 中定义 tiling 数据结构
2. 在 `src/` 中编写 kernel 实现 (`.cpp` 文件)
3. 在 `src/CMakeLists.txt` 中添加 `ascendc_library()` 条目
4. `cmake --build build` 构建

**Kernel 规范:**
- 使用 `extern "C" __global__ __aicore__` 声明 kernel 函数 (仅放在 `.cpp` 中，不要放头文件)
- 参数统一使用 `GM_ADDR` 类型
- 通过 `GM_ADDR` 参数传递 tiling 数据
- Include `"kernel_operator.h"` 获取 AscendC API
- 命名空间: `using namespace AscendC;`

## 目录结构

```
AscendC/
├── CMakeLists.txt              # 顶层构建 (含 CANN 工具链配置)
├── cmake/
│   └── FindCANN.cmake          # CANN 安装路径探测
├── include/
│   └── vector_add_kernel.h     # Tiling 结构体定义
├── src/
│   ├── CMakeLists.txt          # ascendc_library() 编译规则
│   └── vector_add_kernel.cpp   # Kernel 实现
├── test/
│   └── test_vector_add.cpp     # Host 侧测试 (需 NPU / 仿真器)
├── scripts/
│   └── env.sh                  # 环境配置 (供宿主机侧使用)
└── .gitignore
```

## 开发无 NPU 时的方案

| 方案 | 状态 |
|------|------|
| **CPU 仿真器** | ✅ 已配置 — `python3 test/run_sim_test.py` |
| **华为云 Ascend 实例** | ModelArts / 云服务器提供 Ascend 910 算力 |
| **Atlas 200I DK A2** | 开发者套件, 含 Ascend 310B NPU |
| **仅编译验证** | `cmake --build build` 检查语法 |

## 环境变量参考

| 变量 | 值 | 说明 |
|------|-----|------|
| ASCEND_TOOLKIT_HOME | /usr/local/Ascend/ascend-toolkit/latest | CANN 根路径 |
| ASCEND_CANN_PACKAGE_PATH | ${ASCEND_TOOLKIT_HOME} | CMake 构建必需 |
| SOC_VERSION | ascend910b | 目标 NPU 架构 |
| CMAKE_BUILD_TYPE | Release | 构建类型 (Debug / Release) |
