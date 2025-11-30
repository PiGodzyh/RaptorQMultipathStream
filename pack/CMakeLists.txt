cmake_minimum_required(VERSION 3.10)
project(RQPack VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 libRaptorQ 库
# 假设 libRaptorQ 已经编译并安装在 ../libRaptorQ/build 目录
set(LIBRAPTORQ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libRaptorQ")
set(LIBRAPTORQ_BUILD_DIR "${LIBRAPTORQ_DIR}/build")

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRAPTORQ_DIR}/src
)

# 链接目录
link_directories(
    ${LIBRAPTORQ_BUILD_DIR}/lib
)

# 创建 RQPack 静态库
add_library(rqpack STATIC
    rq_pack.cpp
)

# 链接 libRaptorQ
target_link_libraries(rqpack
    RaptorQ
    pthread
)

# 设置库的头文件
target_include_directories(rqpack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 创建测试程序
add_executable(test_pack
    test_pack.cpp
)

target_link_libraries(test_pack
    rqpack
    RaptorQ
    pthread
)

# 创建示例程序
add_executable(example
    example.cpp
)

target_link_libraries(example
    rqpack
    RaptorQ
    pthread
)

# 安装规则
install(TARGETS rqpack
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES rq_pack.h
    DESTINATION include
)

# 打印信息
message(STATUS "RQPack 配置完成")
message(STATUS "  libRaptorQ 目录: ${LIBRAPTORQ_DIR}")
message(STATUS "  libRaptorQ 构建目录: ${LIBRAPTORQ_BUILD_DIR}")

