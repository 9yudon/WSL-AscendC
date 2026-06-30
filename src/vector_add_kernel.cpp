/**
 * VectorAdd - AscendC 算子 (CANN 8.0.0)
 *
 * 计算: C[i] = A[i] + B[i]
 *
 * 验证状态: ✅ WSL2 CPU 仿真通过 (max_diff = 0.0)
 *
 * 参数顺序 (框架约定):
 *   inputs[0]=a, inputs[1]=b, outputs[0]=c, tiling
 *
 * 注意: kernel 参数个数必须与框架传参匹配:
 *   框架传参 = len(inputs) + len(outputs) + (workspace?) + (tiling?1:0)
 */

#include "kernel_operator.h"
#include "vector_add_kernel.h"

using namespace AscendC;

extern "C" __global__ __aicore__ void vector_add_kernel(
    GM_ADDR a,
    GM_ADDR b,
    GM_ADDR c,
    GM_ADDR tiling)
{
    auto *pTiling = reinterpret_cast<__gm__ VectorAddTilingData *>(tiling);
    uint32_t totalLength = pTiling->totalLength;
    uint32_t blockDim    = pTiling->blockDim;

    uint32_t tileCount = (totalLength + blockDim - 1) / blockDim;
    if (tileCount == 0) return;

    auto *pA = reinterpret_cast<__gm__ float *>(a);
    auto *pB = reinterpret_cast<__gm__ float *>(b);
    auto *pC = reinterpret_cast<__gm__ float *>(c);

    GlobalTensor<float> gm_A, gm_B, gm_C;
    gm_A.SetGlobalBuffer(pA, totalLength);
    gm_B.SetGlobalBuffer(pB, totalLength);
    gm_C.SetGlobalBuffer(pC, totalLength);

    TPipe pipe;
    TBuf<> bufA, bufB, bufC;
    pipe.InitBuffer(bufA, blockDim * sizeof(float));
    pipe.InitBuffer(bufB, blockDim * sizeof(float));
    pipe.InitBuffer(bufC, blockDim * sizeof(float));

    for (uint32_t i = 0; i < tileCount; ++i) {
        uint32_t offset = i * blockDim;
        uint32_t thisBlock = (i == tileCount - 1) ? (totalLength - offset) : blockDim;

        // CopyIn: A → UB, B → UB
        LocalTensor<float> tA = bufA.AllocTensor<float>();
        DataCopy(tA, gm_A[offset], thisBlock);
        bufA.EnQue(tA);
        tA = bufA.DeQue<float>();

        LocalTensor<float> tB = bufB.AllocTensor<float>();
        DataCopy(tB, gm_B[offset], thisBlock);
        bufB.EnQue(tB);
        tB = bufB.DeQue<float>();

        // Compute: C = A + B
        LocalTensor<float> tC = bufC.AllocTensor<float>();
        Add(tC, tA, tB, static_cast<int32_t>(thisBlock));
        bufC.EnQue(tC);
        tC = bufC.DeQue<float>();

        // CopyOut: C → GM
        DataCopy(gm_C[offset], tC, thisBlock);
        bufC.EnQue(tC);
        bufC.DeQue<float>();

        bufA.FreeTensor(tA);
        bufB.FreeTensor(tB);
        bufC.FreeTensor(tC);
    }
}
