/**
 * VectorAdd Host 侧测试 (AscendCL)
 *
 * 编译: cmake -DBUILD_TESTS=ON .. && make
 * 运行: ./test_vector_add
 *
 * 前置条件: NPU 驱动 + 固件已安装, 或使用 CANN simulator
 */

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "acl/acl.h"
#include "vector_add_kernel.h"

#define CHECK_ACL(call, msg)                                        \
    do {                                                            \
        aclError _e = (call);                                       \
        if (_e != ACL_SUCCESS) {                                    \
            fprintf(stderr, "[FATAL] %s: %d\n", msg, (int)_e);     \
            exit(1);                                                \
        }                                                           \
    } while (0)

static bool AlmostEqual(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

int main() {
    constexpr uint32_t N = 1024;
    constexpr uint32_t BLOCK_DIM = 256;

    // ---- 1. Host 侧数据 ----------------------------------------------------
    std::vector<float> hA(N), hB(N), hC(N);
    for (uint32_t i = 0; i < N; ++i) {
        hA[i] = static_cast<float>(i);
        hB[i] = static_cast<float>(i * 2);
    }

    // ---- 2. ACL 初始化 -----------------------------------------------------
    CHECK_ACL(aclInit(nullptr), "aclInit");
    CHECK_ACL(aclrtSetDevice(0), "aclrtSetDevice");
    aclrtStream stream;
    CHECK_ACL(aclrtCreateStream(&stream), "aclrtCreateStream");

    // ---- 3. 设备侧内存分配 -------------------------------------------------
    size_t dataBytes = N * sizeof(float);
    float *dA, *dB, *dC;
    CHECK_ACL(aclrtMalloc((void **)&dA, dataBytes, ACL_MEM_MALLOC_HUGE_FIRST), "malloc A");
    CHECK_ACL(aclrtMalloc((void **)&dB, dataBytes, ACL_MEM_MALLOC_HUGE_FIRST), "malloc B");
    CHECK_ACL(aclrtMalloc((void **)&dC, dataBytes, ACL_MEM_MALLOC_HUGE_FIRST), "malloc C");

    // Tiling 数据
    VectorAddTilingData tilingHost = {N, BLOCK_DIM};
    VectorAddTilingData *dTiling;
    CHECK_ACL(aclrtMalloc((void **)&dTiling, sizeof(VectorAddTilingData), ACL_MEM_MALLOC_HUGE_FIRST), "malloc tiling");

    // ---- 4. 数据搬入 --------------------------------------------------------
    CHECK_ACL(aclrtMemcpy(dA, dataBytes, hA.data(), dataBytes, ACL_MEMCPY_HOST_TO_DEVICE), "H2D A");
    CHECK_ACL(aclrtMemcpy(dB, dataBytes, hB.data(), dataBytes, ACL_MEMCPY_HOST_TO_DEVICE), "H2D B");
    CHECK_ACL(aclrtMemcpy(dTiling, sizeof(tilingHost), &tilingHost, sizeof(tilingHost), ACL_MEMCPY_HOST_TO_DEVICE), "H2D tiling");

    // ---- 5. 加载并启动 kernel ----------------------------------------------
    // AscendC kernel 由 CMake 编译为 .o, 运行时通过 AscendCL 加载执行.
    // 示例中使用 ACLRT_LAUNCH_KERNEL 宏 (由 Bisheng 编译器生成).
    //
    // #include "vector_add_kernel.h" 已经声明了 vector_add_kernel,
    // CMake 会自动生成 launch wrapper.
    //
    // 实际调用:
    //   ACLRT_LAUNCH_KERNEL(vector_add_kernel)(1, 1, stream, dA, dB, dC, dTiling);
    //
    // 注意: 上述宏在设备上运行时才有效, CPU 仿真请使用 CANN op_test 框架.

    // ---- 6. 同步 & 搬出结果 -------------------------------------------------
    CHECK_ACL(aclrtSynchronizeStream(stream), "sync stream");
    CHECK_ACL(aclrtMemcpy(hC.data(), dataBytes, dC, dataBytes, ACL_MEMCPY_DEVICE_TO_HOST), "D2H C");

    // ---- 7. 验证 -----------------------------------------------------------
    int errors = 0;
    for (uint32_t i = 0; i < N; ++i) {
        float expected = hA[i] + hB[i];
        if (!AlmostEqual(hC[i], expected)) {
            fprintf(stderr, "Mismatch [%u]: got %f, expected %f\n", i, hC[i], expected);
            if (++errors > 10) break;
        }
    }
    printf("%s  —  %d/%u correct, %d errors\n",
           errors == 0 ? "[PASS] VectorAdd" : "[FAIL] VectorAdd",
           N - errors, N, errors);

    // ---- 8. 清理 -----------------------------------------------------------
    CHECK_ACL(aclrtFree(dA), "free A");
    CHECK_ACL(aclrtFree(dB), "free B");
    CHECK_ACL(aclrtFree(dC), "free C");
    CHECK_ACL(aclrtFree(dTiling), "free tiling");
    CHECK_ACL(aclrtDestroyStream(stream), "destroy stream");
    CHECK_ACL(aclrtResetDevice(0), "reset device");
    CHECK_ACL(aclFinalize(), "aclFinalize");

    return errors == 0 ? 0 : 1;
}
