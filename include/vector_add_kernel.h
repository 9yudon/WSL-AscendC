#ifndef VECTOR_ADD_KERNEL_H_
#define VECTOR_ADD_KERNEL_H_

#include <cstdint>

// ---- Tiling 数据 (host 侧填充, device 侧读取) --------------------------------
struct VectorAddTilingData {
    uint32_t totalLength; // 总元素个数
    uint32_t blockDim;    // 每次循环处理的元素数
};

#endif
