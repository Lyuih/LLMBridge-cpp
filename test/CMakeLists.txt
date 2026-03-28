# 最低cmake版本
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(LLMTest)

#C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#设置构建类型
set(CMAKE_BUILD_TYPE Debug)

#添加可执行文件
add_executable(LLMTest testLLM.cc ../sdk/src/DeepSeekProvider.cc ../sdk/src/GeminiProvider.cc ../sdk/src/GPTProvider.cc ../sdk/src/LLMManager.cc ../sdk/src/logger.cc)

#设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build)

#添加头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)

#查找OpenSSL包
find_package(OpenSSL REQUIRED)

#包含OpennSSL头文件路径
include_directories(${OPENSSL_INCLUDE_DIR})

#添加编译定义
target_compile_definitions(LLMTest PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)

#设置库的目录
link_directories(/usr/local/lib)

#连接库
target_link_libraries(LLMTest fmt jsoncpp OpenSSL::SSL OpenSSL::Crypto spdlog gtest)

