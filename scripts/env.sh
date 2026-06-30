#!/usr/bin/env bash
# ==========================================================================
#  AscendC 开发环境配置 (WSL2 / Linux)
#
#  用法:
#    source scripts/env.sh
#    # 或者在 ~/.bashrc 中加入: source /path/to/AscendC/scripts/env.sh
#
#  验证:
#    echo $ASCEND_TOOLKIT_HOME
#    which ccec
# ==========================================================================

# ---- 1. CANN 安装路径 -------------------------------------------------------
if [ -z "$ASCEND_TOOLKIT_HOME" ]; then
    # 自动探测
    if [ -d "/usr/local/Ascend/ascend-toolkit/latest" ]; then
        export ASCEND_TOOLKIT_HOME="/usr/local/Ascend/ascend-toolkit/latest"
    elif [ -d "/opt/Ascend/ascend-toolkit/latest" ]; then
        export ASCEND_TOOLKIT_HOME="/opt/Ascend/ascend-toolkit/latest"
    else
        echo "[WARN] CANN not found. Install CANN and re-source this script."
        return 0
    fi
fi

# ---- 2. 加载 CANN 官方环境 --------------------------------------------------
# CANN 自带的 set_env.sh 会设置 PATH, LD_LIBRARY_PATH, PYTHONPATH 等
if [ -f "${ASCEND_TOOLKIT_HOME}/set_env.sh" ]; then
    source "${ASCEND_TOOLKIT_HOME}/set_env.sh"
else
    # 手工设置最小环境
    export PATH="${ASCEND_TOOLKIT_HOME}/bin:${ASCEND_TOOLKIT_HOME}/compiler/ccec_compiler/bin:${ASCEND_TOOLKIT_HOME}/tools/ccec_compiler/bin:$PATH"
    export LD_LIBRARY_PATH="${ASCEND_TOOLKIT_HOME}/lib64:${ASCEND_TOOLKIT_HOME}/x86_64-linux/lib64:$LD_LIBRARY_PATH"
    export PYTHONPATH="${ASCEND_TOOLKIT_HOME}/python/site-packages:$PYTHONPATH"
fi

# ---- 3. AscendC 构建所需变量 ------------------------------------------------
export ASCEND_CANN_PACKAGE_PATH="${ASCEND_TOOLKIT_HOME}"
export ASCEND_HOME="${ASCEND_TOOLKIT_HOME}"
export ASCEND_SDK_PATH="${ASCEND_TOOLKIT_HOME}/x86_64-linux"

# ---- 4. SoC 版本 (构建时必须) -----------------------------------------------
# ascend310p3: AI Core m200, WSL2 仿真器可用
# ascend910b:  支持 dynamic shape, 需 910B 仿真器
export SOC_VERSION="${SOC_VERSION:-ascend310p3}"

# ---- 5. 仿真器配置 -----------------------------------------------------------
export SIMULATOR_PATH="${ASCEND_SDK_PATH}/simulator"
# 仿真器需要加载这些 .so, 框架的 Python LD_LIBRARY_PATH 设置不够覆盖全部依赖
export LD_LIBRARY_PATH="${SIMULATOR_PATH}/Ascend310P1/lib:${ASCEND_SDK_PATH}/lib64:${ASCEND_TOOLKIT_HOME}/lib64:${ASCEND_SDK_PATH}/lib64/plugin/opskernel:$LD_LIBRARY_PATH"

# ---- 6. 常用别名 ------------------------------------------------------------
alias bb="mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j\$(nproc)"
alias cdb="cd \$(git rev-parse --show-toplevel 2>/dev/null || pwd)/build"
alias simtest="python3 \$(git rev-parse --show-toplevel 2>/dev/null || pwd)/test/run_sim_test.py"

echo "[OK] CANN environment loaded"
echo "     ASCEND_TOOLKIT_HOME = ${ASCEND_TOOLKIT_HOME}"
echo "     SOC_VERSION         = ${SOC_VERSION}"
echo "     SIMULATOR_PATH      = ${SIMULATOR_PATH}"
echo "     ccec                = $(which ccec 2>/dev/null || echo 'NOT FOUND')"
