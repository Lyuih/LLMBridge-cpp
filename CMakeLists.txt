# LLMBridge 总构建入口
# 依赖默认使用 third_party 内嵌版本,开箱即用;gRPC 服务端为可选项(需要系统安装 gRPC/Protobuf)
cmake_minimum_required(VERSION 3.10)
project(LLMBridge LANGUAGES C CXX)

# C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# 源码为 UTF-8 编码,MSVC 需要显式声明,否则中文注释/字符串会按本地代码页误读
if(MSVC)
  add_compile_options(/utf-8)
endif()

# 内嵌第三方依赖目录
set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# 输出目录统一到 build/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build)

add_subdirectory(sdk)
add_subdirectory(test)
add_subdirectory(demo)

message(STATUS "LLMBridge 构建完成配置, 内嵌依赖目录: ${THIRD_PARTY}")
