#!/usr/bin/env python3
"""
VectorAdd AscendC 仿真测试 (WSL2 / CPU Simulator)

依赖: CANN 8.0.0 + numpy + op_test_frame

用法:
    cd /mnt/d/AscendC
    source /usr/local/Ascend/ascend-toolkit/set_env.sh
    export SIMULATOR_PATH=/usr/local/Ascend/ascend-toolkit/latest/x86_64-linux/simulator
    python3 test/run_sim_test.py
"""

import os
import sys
import struct
import subprocess
import numpy as np

# ---- 配置 ----------------------------------------------------------------
ASCEND_TOOLKIT = os.environ.get("ASCEND_TOOLKIT_HOME",
    "/usr/local/Ascend/ascend-toolkit/latest")
SIMULATOR_PATH = os.environ.get("SIMULATOR_PATH",
    f"{ASCEND_TOOLKIT}/x86_64-linux/simulator")
SOC_VERSION = "Ascend310P1"   # 纯 AI Core (m200), 与 ascend310p3 对应
SIM_MODE = "pv"

# 确保仿真器和 CANN 运行时库在链接路径中
_cann_lib = f"{ASCEND_TOOLKIT}/x86_64-linux/lib64"
_sim_lib = f"{SIMULATOR_PATH}/{SOC_VERSION}/lib"
_plugin_lib = f"{_cann_lib}/plugin/opskernel"
_ld_paths = [_sim_lib, _cann_lib, f"{ASCEND_TOOLKIT}/lib64", _plugin_lib]
for _p in _ld_paths:
    os.environ["LD_LIBRARY_PATH"] = f"{_p}:" + os.environ.get("LD_LIBRARY_PATH", "")

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
KERNEL_CPP = os.path.join(PROJECT_DIR, "src", "vector_add_kernel.cpp")
KERNEL_INCLUDE = os.path.join(PROJECT_DIR, "include")
TIKCFW = f"{ASCEND_TOOLKIT}/tools/tikcpp/tikcfw"
OUTPUT_DIR = "/tmp/ascendc_test"


# ---- Step 1: 编译 kernel (ccec 直接编译) -----------------------------------
def compile_kernel():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    kernel_o = os.path.join(OUTPUT_DIR, "vector_add_kernel.o")

    # 优先使用 CMake 构建的 kernel (经过了正确的 auto-gen 包装)
    cmake_kernel = "/usr/local/single_objects/vector_add_kernel/vector_add_kernel.o"
    if os.path.exists(cmake_kernel):
        print(f"[compile] Using CMake-built kernel: {cmake_kernel}")
        print(f"[compile]   size = {os.path.getsize(cmake_kernel)} bytes")
        return cmake_kernel

    ccec = f"{ASCEND_TOOLKIT}/compiler/ccec_compiler/bin/ccec"

    cmd = [
        ccec, "-c", "-x", "cce", "-O2",
        KERNEL_CPP, "-o", kernel_o, "-std=c++17",
        "--cce-aicore-arch=dav-m200",
        "--cce-aicore-only",
        "-mllvm", "-cce-aicore-function-stack-size=16000",
        "-mllvm", "-cce-aicore-fp-ceiling=2",
        "-mllvm", "-cce-aicore-record-overflow=False",
        f"-I{TIKCFW}",
        f"-I{TIKCFW}/impl",
        f"-I{TIKCFW}/interface",
        f"-I{KERNEL_INCLUDE}",
    ]

    print(f"[compile] ccec ...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[compile] FAILED\n{result.stdout}\n{result.stderr}")
        return None
    print(f"[compile] OK -> {kernel_o}  ({os.path.getsize(kernel_o)} bytes)")
    return kernel_o


# ---- Step 2: 仿真运行 -----------------------------------------------------
def run_simulation(kernel_o: str):
    import ctypes
    from op_test_frame.runtime.rts_api import AscendRTSApi
    from op_test_frame.common.ascend_tbe_op import AscendOpKernel, AscendOpKernelRunner

    # ---- 预加载运行时依赖 (仿真器 .so 依赖这些库) -------------------------
    for dep in ["libc_sec.so", "libmmpa.so", "libascendalog.so", "libplatform.so"]:
        p = f"{_cann_lib}/{dep}"
        try:
            ctypes.CDLL(p, mode=ctypes.RTLD_GLOBAL)
        except Exception:
            pass  # may already be loaded

    N = 256
    BLOCK_DIM = 256

    a_data = np.arange(N, dtype=np.float32)
    b_data = np.arange(N, dtype=np.float32) * 2.0
    expected = a_data + b_data

    # Tiling data: VectorAddTilingData { totalLength: uint32, blockDim: uint32 }
    tiling_bytes = struct.pack("<II", N, BLOCK_DIM)

    # ---- AscendOpKernel (parses kernel name from source) -----------------
    ascendc_cfg = {
        "block_dim": 1,
        "ascendc_op_path": KERNEL_CPP,
    }
    op_kernel = AscendOpKernel(kernel_o, ascendc_cfg=ascendc_cfg)

    # 确保 kernel 注册了输入/输出信息
    op_kernel.set_input_info([
        {"shape": [N],  "dtype": "float32", "param_type": "input"},
        {"shape": [N],  "dtype": "float32", "param_type": "input"},
    ])
    op_kernel.set_output_info([
        {"shape": [N],  "dtype": "float32", "param_type": "output"},
    ])

    # Kernel 有 5 个 GM_ADDR 参数: a, b, c, workspace, tiling
    # workspace 不使用, tiling 放最后
    op_kernel.need_do_tiling = True

    # ---- 启动仿真器 ------------------------------------------------------
    # simulator_lib_path 应为 simulator 根目录, Runner 会自动拼 soc_version/lib
    sim_lib_root = SIMULATOR_PATH
    sim_dump_path = os.path.join(OUTPUT_DIR, "dump")
    os.makedirs(sim_dump_path, exist_ok=True)

    print(f"[sim] mode={SIM_MODE}  soc={SOC_VERSION}  lib_root={sim_lib_root}")
    with AscendOpKernelRunner(
        simulator_mode=SIM_MODE,
        soc_version=SOC_VERSION,
        simulator_lib_path=sim_lib_root,
        simulator_dump_path=sim_dump_path,
    ) as runner:
        output_data_list = runner.run(
            op_kernel,
            inputs=[a_data, b_data],
            tiling=tiling_bytes,
        )

        # 必须在 with 块内 sync, 因为 __exit__ 会 reset device 释放内存
        if not isinstance(output_data_list, (list, tuple)):
            output_data_list = [output_data_list]
        if len(output_data_list) == 0:
            print("\n[FAIL]  No output data returned.")
            return False

        output_data = output_data_list[0]
        if hasattr(output_data, 'sync_from_device'):
            output_data.sync_from_device()
        result = output_data.get_data() if hasattr(output_data, 'get_data') else output_data
        if result is None:
            print("[ERROR] get_data() returned None")
            return False
        if isinstance(result, np.ndarray):
            result = result.flatten()[:N].astype(np.float32)
        elif isinstance(result, bytes):
            result = np.frombuffer(result, dtype=np.float32)[:N]

        print(f"\n[result] first 8: {result[:8]}")
        print(f"[expect] first 8: {expected[:8]}")

        diff = np.abs(result - expected)
        max_diff = float(np.max(diff))
        print(f"[result] max diff = {max_diff:.6e}")

        if max_diff < 1e-3:
            print(f"\n[PASS]  VectorAdd simulation test passed!")
            return True
        else:
            print(f"\n[FAIL]  VectorAdd simulation test failed.")
            return False


# ---- main ---------------------------------------------------------------
if __name__ == "__main__":
    print("=" * 60)
    print("  AscendC VectorAdd Simulator Test (WSL2)")
    print("=" * 60)
    print(f"  SOC:        {SOC_VERSION}")
    print(f"  Sim mode:   {SIM_MODE}")
    print(f"  Sim path:   {SIMULATOR_PATH}")
    print(f"  Kernel:     {KERNEL_CPP}")
    print()

    kernel_o = compile_kernel()
    if kernel_o is None:
        print("\n[ABORT] Compilation failed.")
        sys.exit(1)

    try:
        ok = run_simulation(kernel_o)
        sys.exit(0 if ok else 1)
    except Exception as e:
        print(f"\n[ERROR] {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
