#include "kernel_operator.h"

extern "C" __global__ __aicore__ void flash_attention(GM_ADDR query, GM_ADDR key, GM_ADDR value, GM_ADDR atten_mask, GM_ADDR attention_out, GM_ADDR workspace, GM_ADDR tiling) {
    GET_TILING_DATA(tiling_data, tiling);
    // TODO: user kernel impl
}