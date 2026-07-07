# AscendC 自定义算子工程搭建指南 — msopgen 工具使用

## 1. 环境准备

### 1.1 WSL 多发行版环境

当前开发环境为 Windows 11 + WSL2，存在两个 WSL 发行版：

| 发行版 | 用途 |
|--------|------|
| `Ubuntu-24.04` | 代码编辑、Git 管理（当前工作区 `D:\AscendC`） |
| `AscendUbuntu` | CANN 工具包安装环境 |

文件共享：WSL2 发行版之间通过 `/mnt/<盘符>/` 共享 Windows 文件系统，因此 `D:\AscendC` 在两个发行版中均可通过 `/mnt/d/AscendC` 访问。

### 1.2 CANN 工具包版本

- 安装路径：`/usr/local/Ascend/ascend-toolkit/`
- 版本：`8.0.0`（`latest` 软链接指向此版本）
- msopgen 路径：`/usr/local/Ascend/ascend-toolkit/latest/python/site-packages/bin/msopgen`

### 1.3 跨发行版执行命令

从 `Ubuntu-24.04` 在 `AscendUbuntu` 中执行命令的方式：

```bash
# 方式一：管道传命令（推荐，避免 Windows 路径翻译问题）
echo '<commands>' | wsl -d AscendUbuntu -u root

# 方式二：直接执行（路径可能被 Windows 翻译导致失败）
wsl -d AscendUbuntu -- bash -c '<commands>'
```

**注意**：WSL 的 Windows interop 会将 `/usr/` 路径错误翻译为 `E:/Git/usr/`，因此推荐使用管道方式或将文件复制到 WSL 原生文件系统后再操作。

---

## 2. msopgen 工具详解

### 2.1 工具位置与激活

```bash
# 工具路径
/usr/local/Ascend/ascend-toolkit/latest/python/site-packages/bin/msopgen

# 使用前必须 source 环境
source /usr/local/Ascend/ascend-toolkit/set_env.sh
```

**不 source 的后果**：`ModuleNotFoundError: No module named 'op_gen'`

### 2.2 命令总览

```bash
msopgen [-h] {mi, gen, compile, sim} ...
```

| 子命令 | 功能 | 使用场景 |
|--------|------|----------|
| `gen` | 生成算子工程 | 从 JSON 定义创建完整项目骨架 |
| `compile` | 编译算子工程 | 编译已有项目 |
| `sim` | 仿真工具 | 将 dump 数据转换为 trace 格式可视化 |
| `mi` | IDE 机器接口 | 供 IDE 调用，查询算子信息 |

### 2.3 `gen` 命令参数（核心）

```bash
msopgen gen -i <JSON文件> -c <计算单元> [-f <框架>] [-lan cpp/py] [-out 输出目录] [-m 模式]
```

| 参数 | 必需 | 说明 |
|------|------|------|
| `-i, --input` | **是** | 输入 JSON 原型定义文件 |
| `-c, --compute_unit` | **是** | 计算单元：`ai_core-ascend910b`、`aicpu`、`vector_core-ascend610` 等 |
| `-f, --framework` | 否 | 框架：`aclnn`、`pytorch`、`tf`/`tensorflow`、`onnx`、`ms`/`mindspore`、`caffe` |
| `-lan, --language` | 否 | `py`=DSL/TIK(默认), `cpp`=AscendC C/C++ |
| `-out, --output` | 否 | 输出目录（默认当前目录） |
| `-m, --mode` | 否 | `0`=新建工程(默认), `1`=追加到已有工程 |
| `-op, --operator` | 否 | 配合 `-m 1`，指定要追加的算子名 |

### 2.4 `compile` / `sim` 命令

```bash
# 编译
msopgen compile -i <项目路径> [-c CANN_PATH] [-q]

# 仿真（将 dump 数据可视化）
msopgen sim -c <core_id> -d <dump_dir> -out <输出目录> [-subc <subcore_id>] [-mix]
```

---

## 3. JSON 输入文件格式

### 3.1 完整 JSON Schema

```json
[
    {
        "op": "OperatorName",
        "input_desc": [
            {
                "name": "参数名",
                "param_type": "required",
                "format": ["ND"],
                "type": ["float16", "float32"]
            }
        ],
        "output_desc": [
            {
                "name": "参数名",
                "param_type": "required",
                "format": ["ND"],
                "type": ["float16", "float32"]
            }
        ],
        "attr": [
            {
                "name": "属性名",
                "param_type": "optional",
                "type": "int",
                "default_value": 0
            }
        ]
    }
]
```

### 3.2 字段说明

**顶层字段**：

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `op` | string | 是 | 算子名，UpperCamelCase，如 `FlashAttention` |
| `input_desc` | array | 否 | 输入张量描述列表 |
| `output_desc` | array | **是** | 输出张量描述列表 |
| `attr` | array | 否 | 属性（标量参数）描述列表 |

**input_desc / output_desc 子字段**：

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 参数名，snake_case |
| `param_type` | string | 否 | `required`(默认)、`optional`、`dynamic` |
| `format` | array | 是 | 数据排布：`ND`、`NHWC`、`NCHW`、`FRACTAL_NZ` 等 |
| `type` | array | 是 | 数据类型，见下方类型表 |

**attr 子字段**：

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 属性名，snake_case |
| `param_type` | string | 否 | `required`(默认)、`optional` |
| `type` | string | 是 | `int`、`float`、`bool`、`string`、`listInt`、`listFloat`、`listBool`、`listString` |
| `default_value` | — | optional时必填 | 默认值 |

### 3.3 支持的数据类型 (type)

```
float16 / fp16     float32 / fp32 / float     bfloat16 / bf16
half               double                      int8 / int16 / int32 / int64
uint8 / uint16 / uint32 / uint64              bool
complex64          complex128
```

### 3.4 关键规则（踩坑总结）

**规则 1：所有输入/输出的 `type` 数组长度必须一致**

这是最容易出错的点。多个输入/输出之间采用**纵向配对**逻辑：

```
输入1: type=["int8",   "fp16",  "fp32"]
输入2: type=["fp16",   "fp32",  "fp32"]
输出1: type=["int32",  "fp16",  "fp32"]
```

第一列 `(int8, fp16) → int32`、第二列 `(fp16, fp32) → fp16`、第三列 `(fp32, fp32) → fp32` — 各列独立，不存在交叉匹配。

**错误示例**：
```json
// 错误！query 有 3 个 type，atten_mask 只有 2 个 type
{"name": "query",    "type": ["float16", "float32", "bfloat16"]}  // 3项
{"name": "atten_mask", "type": ["float16", "bfloat16"]}           // 2项 → 报错！
```

**规则 2：`format` 数组可自动扩展**

- `format` 只有 1 项 → 自动扩展为与 `type` 等长
- `format` 与 `type` 数量不同 → **报错**

**规则 3：权限问题（WSL + NTFS 特有）**

JSON 文件在 Windows 文件系统（NTFS）上权限为 `rwxrwxrwx`(777)，msopgen 安全校验会拒绝。**解法**：将 JSON 复制到 WSL 原生 ext4 文件系统（如 `/tmp/`）再执行。

---

## 4. FlashAttention 算子工程生成实录

### 4.1 JSON 原型定义

```json
[
    {
        "op": "FlashAttention",
        "input_desc": [
            {
                "name": "query",
                "param_type": "required",
                "format": ["ND"],
                "type": ["float16", "float"]
            },
            {
                "name": "key",
                "param_type": "required",
                "format": ["ND"],
                "type": ["float16", "float"]
            },
            {
                "name": "value",
                "param_type": "required",
                "format": ["ND"],
                "type": ["float16", "float"]
            },
            {
                "name": "atten_mask",
                "param_type": "optional",
                "format": ["ND"],
                "type": ["float16", "float"]
            }
        ],
        "output_desc": [
            {
                "name": "attention_out",
                "param_type": "required",
                "format": ["ND"],
                "type": ["float16", "float"]
            }
        ],
        "attr": [
            {"name": "scale_value",  "param_type": "optional", "type": "float", "default_value": 0.0},
            {"name": "keep_prob",    "param_type": "optional", "type": "float", "default_value": 1.0},
            {"name": "pre_tokens",   "param_type": "optional", "type": "int",   "default_value": 65536},
            {"name": "next_tokens",  "param_type": "optional", "type": "int",   "default_value": 65536},
            {"name": "head_num",                             "type": "int",   "default_value": 32},
            {"name": "inner_precise","param_type": "optional", "type": "int",   "default_value": 0},
            {"name": "sparse_mode",  "param_type": "optional", "type": "int",   "default_value": 0}
        ]
    }
]
```

### 4.2 生成命令

```bash
# 1. 复制 JSON 到 WSL 原生文件系统（避免 NTFS 权限问题）
mkdir -p /tmp/flash_attention
cp /mnt/d/AscendC/flash_attention/flash_attention.json /tmp/flash_attention/

# 2. 激活 CANN 环境
source /usr/local/Ascend/ascend-toolkit/set_env.sh

# 3. 生成工程
msopgen gen \
  -i "/tmp/flash_attention/flash_attention.json" \
  -f aclnn \
  -c "ai_core-ascend910b" \
  -lan cpp \
  -out "/tmp/flash_attention/FlashAttention"

# 4. 复制回 Windows 工作区
cp -r /tmp/flash_attention/FlashAttention /mnt/d/AscendC/flash_attention/
```

### 4.3 生成的工程结构

```
D:\AscendC\flash_attention\FlashAttention\
├── CMakeLists.txt                          # 顶层 CMake 配置
├── build.sh                                # 编译入口脚本
├── cmake/
│   ├── config.cmake                        # 编译器/依赖路径配置
│   ├── func.cmake                          # CMake 通用函数
│   ├── intf.cmake                          # CMake 接口定义
│   └── util/                               # 构建工具脚本集
│       ├── ascendc_bin_param_build.py      # 二进制参数构建
│       ├── ascendc_compile_kernel.py       # Kernel 编译
│       ├── ascendc_impl_build.py           # 实现文件构建
│       ├── ascendc_op_info.py              # 算子信息处理
│       ├── ascendc_ops_config.py           # 算子配置
│       ├── ascendc_pack_kernel.py          # Kernel 打包
│       ├── opdesc_parser.py                # 算子描述解析
│       └── preset_parse.py                # 预设配置解析
├── op_host/
│   ├── CMakeLists.txt
│   ├── flash_attention.cpp                 # ★ Host 侧：算子注册 + Tiling + 推断
│   └── flash_attention_tiling.h            # ★ Tiling 数据结构定义
└── op_kernel/
    ├── CMakeLists.txt
    └── flash_attention.cpp                 # ★ Kernel 侧：核心计算逻辑（待填充）
```

### 4.4 自动生成的关键代码

**op_host/flash_attention.cpp** — 包含三部分：

1. **Tiling 函数** (`optiling::TilingFunc`)：计算 data size、设置 BlockDim(8)
2. **Shape/DataType 推断** (`InferShape`, `InferDataType`)：output shape 从 input[0] 继承
3. **算子注册类** (`ops::FlashAttention`)：声明 4 输入、1 输出、7 属性，注册 ascend910 配置

**op_host/flash_attention_tiling.h** — 当前仅含 `uint32_t size`：

```cpp
BEGIN_TILING_DATA_DEF(FlashAttentionTilingData)
  TILING_DATA_FIELD_DEF(uint32_t, size);
END_TILING_DATA_DEF;
```

**op_kernel/flash_attention.cpp** — Kernel 骨架（核心计算逻辑待填充）：

```cpp
#include "kernel_operator.h"

extern "C" __global__ __aicore__ void flash_attention(
    GM_ADDR query, GM_ADDR key, GM_ADDR value, GM_ADDR atten_mask,
    GM_ADDR attention_out, GM_ADDR workspace, GM_ADDR tiling) {
    GET_TILING_DATA(tiling_data, tiling);
    // TODO: user kernel impl
}
```

---

## 5. 后续工作

生成工程后，需要手动完成以下步骤才能得到可运行的算子：

1. **扩展 Tiling 数据** (`flash_attention_tiling.h`)：
   - 添加 seq_len(Q)、seq_len(KV)、head_dim、num_heads、分块大小等字段

2. **填充 Kernel 计算逻辑** (`op_kernel/flash_attention.cpp`)：
   - 实现 FlashAttention 核心算法：$\text{Attention}(Q,K,V) = \text{softmax}(\frac{QK^T}{\sqrt{d_k}} + \text{mask})V$
   - 使用 AscendC API 的分块、矩阵乘、softmax、mask 等操作

3. **编译与部署**：
   ```bash
   cd FlashAttention
   bash build.sh
   ```

4. **测试验证**：编写测试用例验证精度和性能

---

## 6. 常见问题速查

| 问题 | 原因 | 解法 |
|------|------|------|
| `ModuleNotFoundError: No module named 'op_gen'` | 未 source CANN 环境 | `source /usr/local/Ascend/ascend-toolkit/set_env.sh` |
| `The path ... should not be written by user group or others` | JSON 文件在 NTFS 上权限太宽 | 复制到 `/tmp/` 后再执行 |
| `The number(N) of type for "xxx" is inconsistent with the number(M) of "yyy"` | 各 tensor 的 type 数组长度不一致 | 确保所有输入/输出的 type 数组长度相同 |
| `invalid number count for type and format in xxx` | format 数组长度 != type 数组长度 且 format 长度 > 1 | 统一 format 和 type 数量，或 format 只用 1 项靠自动扩展 |
| `E:/Git/usr/... No such file or directory` | WSL interop 翻译了路径 | 用管道方式传命令：`echo 'cmd' \| wsl -d AscendUbuntu` |
