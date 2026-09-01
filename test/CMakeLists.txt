# LLMTest: gtest 单元测试,链接 SDK 核心库

if(NOT DEFINED THIRD_PARTY)
  set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
endif()

set(GTEST_DIR ${THIRD_PARTY}/googletest-1.14.0)

# 构建内嵌 gtest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(${GTEST_DIR} gtest_build)

# 测试可执行文件
add_executable(LLMTest testLLM.cc)
target_link_libraries(LLMTest PRIVATE llmbridge gtest)

# testLLM.cc 自带头文件,保证能找到 SDK 头
target_include_directories(LLMTest PRIVATE ../sdk/include)

# 链接需要
enable_testing()
add_test(NAME llm_core_tests COMMAND LLMTest)
