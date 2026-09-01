# cli_demo: 命令行交互 demo,链接 SDK 核心库

if(NOT DEFINED THIRD_PARTY)
  set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
endif()

set(JSONCPP_INCLUDE ${THIRD_PARTY}/jsoncpp-1.9.6/include)

add_executable(cli_demo cli_demo.cc)
target_link_libraries(cli_demo PRIVATE llmbridge)
target_include_directories(cli_demo PRIVATE ${JSONCPP_INCLUDE})
