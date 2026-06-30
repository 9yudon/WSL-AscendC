# ---- VectorAdd kernel ------------------------------------------------------
# ascendc_library() 调用 Bisheng 交叉编译器生成 NPU 目标码
# 编译后的 .o 在 build/src/ 下

ascendc_library(vector_add_kernel OBJECT
    vector_add_kernel.cpp
)

# 将项目 include 目录暴露给依赖本 kernel 的 host 侧 target
ascendc_include_directories(vector_add_kernel PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
)
