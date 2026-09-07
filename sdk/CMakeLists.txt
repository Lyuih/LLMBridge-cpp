# SDK 核心静态库 + 可选 gRPC 服务端
# 依赖优先使用 third_party 内嵌版本,保证开箱即用
# gRPC 服务端需要系统安装 gRPC/Protobuf/OpenSSL,未找到时自动跳过

if(NOT DEFINED THIRD_PARTY)
  set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
endif()

set(FMT_DIR          ${THIRD_PARTY}/fmt-9.1.0)
set(FMT_INCLUDE      ${FMT_DIR}/include)
set(SPDLOG_INCLUDE   ${THIRD_PARTY}/spdlog-1.14.1/include)
set(JSONCPP_INCLUDE  ${THIRD_PARTY}/jsoncpp-1.9.6/include)
set(JSONCPP_SRC      ${THIRD_PARTY}/jsoncpp-1.9.6/src/lib_json)
set(SQLITE3_DIR      ${THIRD_PARTY}/sqlite-amalgamation-3450300)

# ---------- sqlite3 (单文件 C 库) ----------
add_library(sqlite3 STATIC ${SQLITE3_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${SQLITE3_DIR})
set_target_properties(sqlite3 PROPERTIES LANGUAGE C)

# ---------- fmt (spdlog 依赖其编译库符号) ----------
add_library(fmt STATIC ${FMT_DIR}/src/format.cc)
target_include_directories(fmt PUBLIC ${FMT_INCLUDE})
target_compile_definitions(fmt PUBLIC FMT_HEADER_ONLY=0)

# ---------- SDK 核心库 ----------
add_library(llmbridge STATIC
    src/ClaudeProvider.cc
    src/GeminiProvider.cc
    src/OpenAICompatProvider.cc
    src/ProviderFactory.cc
    src/ConfigLoader.cc
    src/LLMManager.cc
    src/DataManager.cc
    src/ChatSDK.cc
    src/SessionManager.cc
    src/logger.cc
    src/UUIDUtil.cc
    src/Router.cc
    src/ContextManager.cc
    src/MetricsCollector.cc
    src/Tool.cc
    # jsoncpp 源码
    ${JSONCPP_SRC}/json_reader.cpp
    ${JSONCPP_SRC}/json_value.cpp
    ${JSONCPP_SRC}/json_writer.cpp
)

target_include_directories(llmbridge PUBLIC include)
target_include_directories(llmbridge PUBLIC ${FMT_INCLUDE} ${SPDLOG_INCLUDE} ${JSONCPP_INCLUDE} ${JSONCPP_SRC})
target_link_libraries(llmbridge PUBLIC sqlite3 fmt)

# spdlog 使用外部 fmt
target_compile_definitions(llmbridge PUBLIC SPDLOG_FMT_EXTERNAL)

# OpenSSL 可选: 找到则开启 cpp-httplib 的 HTTPS 支持
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  target_compile_definitions(llmbridge PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
  target_link_libraries(llmbridge PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  message(STATUS "OpenSSL 已找到,启用 HTTPS 支持")
else()
  message(WARNING "未找到 OpenSSL,HTTPS 请求将不可用(可编译,运行时仅支持 HTTP)")
endif()

# ---------- gRPC 服务端(可选) ----------
find_package(gRPC CONFIG QUIET)
find_package(Protobuf QUIET)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)

if(gRPC_FOUND AND Protobuf_FOUND AND GRPC_CPP_PLUGIN)
  set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
  set(PROTO_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  file(MAKE_DIRECTORY ${PROTO_BIN_DIR})

  add_custom_command(
    OUTPUT
      ${PROTO_BIN_DIR}/llm_bridge.pb.cc
      ${PROTO_BIN_DIR}/llm_bridge.pb.h
      ${PROTO_BIN_DIR}/llm_bridge.grpc.pb.cc
      ${PROTO_BIN_DIR}/llm_bridge.grpc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            -I${PROTO_SRC_DIR}
            --cpp_out=${PROTO_BIN_DIR}
            --grpc_out=${PROTO_BIN_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${PROTO_SRC_DIR}/llm_bridge.proto
    DEPENDS ${PROTO_SRC_DIR}/llm_bridge.proto
  )

  add_custom_target(generated_proto DEPENDS
      ${PROTO_BIN_DIR}/llm_bridge.pb.cc
      ${PROTO_BIN_DIR}/llm_bridge.pb.h
      ${PROTO_BIN_DIR}/llm_bridge.grpc.pb.cc
      ${PROTO_BIN_DIR}/llm_bridge.grpc.pb.h)

  add_executable(llmbridge_server grpc_server.cc
      ${PROTO_BIN_DIR}/llm_bridge.grpc.pb.cc
      ${PROTO_BIN_DIR}/llm_bridge.pb.cc)
  target_include_directories(llmbridge_server PRIVATE include ${PROTO_BIN_DIR})
  target_link_libraries(llmbridge_server PRIVATE llmbridge
      gRPC::grpc++ gRPC::grpc++_reflection protobuf::libprotobuf
      OpenSSL::SSL OpenSSL::Crypto)
  add_dependencies(llmbridge_server generated_proto)
  message(STATUS "gRPC 服务端 llmbridge_server 已启用(监听 0.0.0.0:50051)")
else()
  message(WARNING "未找到 gRPC/Protobuf/grpc_cpp_plugin,跳过 gRPC 服务端构建。安装后可构建 llmbridge_server。")
endif()
