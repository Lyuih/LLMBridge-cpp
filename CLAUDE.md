# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

LLMBridge 是一个 C++17 的 LLM SDK，以**三大协议族**（OpenAI 兼容 / Anthropic Messages / Gemini）为抽象，为任意模型（DeepSeek、GPT、Claude、Gemini、Ollama 本地等）提供统一的对话接口——新模型只需 JSON 配置接入，并支持：
- **配置驱动 + 插件化注册**：通过 JSON 配置加载任意模型，无需改代码
- **模型路由 / 故障转移**：加权路由 + 熔断 + fallback 链
- **上下文 / Token 管理**：token 估算 + 预算裁剪 + 历史摘要
- **Function Calling**：工具注入、执行循环、结果回填
- **可观测性**：耗时 / token / 错误率统计
- gRPC 服务对外暴露（可选构建）

代码注释、日志和提交信息全部使用中文，请保持该风格。

```
sdk/
  include/   # 所有头文件
  src/       # 各 .cc 实现
  proto/llm_bridge.proto   # gRPC 服务定义(可选)
  grpc_server.cc           # gRPC 服务端入口(可选)
  CMakeLists.txt           # 核心静态库 llmbridge + 可选服务端
test/testLLM.cc            # gtest 单元测试
demo/cli_demo.cc           # CLI 交互 demo
config/models.example.json # 配置驱动示例
third_party/               # 内嵌依赖: fmt/spdlog/jsoncpp/sqlite3/googletest
CMakeLists.txt             # 根构建入口
```

## 构建与测试

依赖全部内嵌在 `third_party/`，开箱即用，无需系统安装任何库。gRPC 服务端是可选目标（需要系统 gRPC/Protobuf/OpenSSL，未找到自动跳过）。

```bash
# 配置 + 构建(生成 build/Debug/llmbridge.lib、LLMTest.exe、cli_demo.exe)
cmake -B build -S .
cmake --build build

# 运行单元测试(全部为无网络的纯逻辑用例)
./build/Debug/LLMTest.exe

# 运行 CLI demo(默认读 ../config/models.example.json,可用参数指定配置)
./build/Debug/cli_demo.exe config/models.example.json
```

Windows 上用 MSVC 构建时，根 CMakeLists 已加 `/utf-8`（源码为 UTF-8 中文，不指定会被按 GBK 误读）。无 OpenSSL 时 httplib 走纯 HTTP 模式（可编译，HTTPS 不可用）。

## 架构分层

数据/调用流自底向上：`LLMProvider`（具体大模型）→ `LLMManager`（provider 注册表）→ `ChatSDK`（门面，含路由/上下文/工具/指标编排）→ gRPC 服务。会话侧由 `SessionManager` → `DataManager`（SQLite 持久化）并行支撑。

**Provider 层**：`LLMProvider` 抽象基类。`sendMessage` 返回结构化 `LLMResponse`（content + tool_calls + 输入/输出 token），支持注入 `tools` 做 Function Calling。**三大协议族**：`OpenAICompatProvider`（OpenAI 兼容，DeepSeek/GPT/Ollama(/v1)/Qwen 等都走它，api_key 可空）、`ClaudeProvider`（Anthropic Messages，x-api-key 鉴权 + tool_use/tool_result block + 独立 SSE 事件族）、`GeminiProvider`（原生 Gemini 协议，硬编码走代理 `127.0.0.1:7897`）。每个 provider 内部统一三步：`buildRequestBody`（jsoncpp 序列化，含工具注入）→ cpp-httplib HTTP POST → `parseResponse`（解析 content / tool_calls / usage token）。流式接口 `sendMessageStream` 返回字符串，逐帧回调 `func_stream = std::function<void(const std::string&, bool)>`。

**配置驱动注册**：`ConfigLoader` 解析 JSON（`models` 数组）→ `vector<shared_ptr<Config>>`，**全部统一为 `ApiConfig`**（`base_url` + `api_key` + `name` 消化一切模型差异）；`ProviderFactory` 按 `provider_type` 字符串创建 provider，`registerBuiltinProviders()` 注册协议族类型（openai/gpt(别名)/claude/gemini），模型的差异走配置、**协议族**的差异才走 `registerProvider(type, creator)` 一行。`ChatSDK::initFromConfigFile(path)` 是配置驱动入口。实现"任意模型"。

**模型路由**：`Router` 组件——`addBackend(model, weight, fallback)` 注册后端，`addRoute(name, backends)` 注册虚拟路由组，`plan(model)` 返回尝试顺序（加权随机主选 + 权重降序备选 + 熔断兜底 + fallback 链）。熔断：调用失败 `markUnavailable` 进入窗口，超时自动恢复（半开）。配置中 `route` 字段定义虚拟路由组，`fallback` 定义故障转移链，`weight` 定义流量权重。

**上下文管理**：`ContextManager`——`estimateTokens` 用字符比例启发式（中文≈0.7 token/字符，英文≈0.25），`fitToBudget` 保留最近消息、把溢出历史摘要为 system 前缀。配置 `context_window` 启用，预算 = context_window - max_tokens。

**Function Calling**：`Tool` 可执行接口 + `ToolRegistry`（注册/执行/生成 schema）。`ChatSDK::sendMessage` 内部执行循环：模型请求工具 → 执行回填 → 再次调用，最多 5 轮。`Message` 支持 `tool_call_id`/`tool_name`（工具结果回填）与 `tool_calls`（助手请求工具）。

**可观测性**：`MetricsCollector` 单例，按模型聚合请求数/成功失败/耗时/token，提供 `toJson()` 导出。`ChatSDK::getMetricsJson()` / `getRouteNames()` / `getToolNames()` 暴露查询。

## 关键注意事项

- **`common.h` 核心结构**：`Message` / `ToolCall` / `Session` / `Config`（含 provider_type、weight、fallback、route、context_window）/ `ApiConfig` / `ModelInfo`。`fields.h` 集中 JSON 字段名常量。
- **函数调用测试**：`test/testLLM.cc` 用 `FakeToolProvider`（经 `ProviderFactory::registerProvider("fake-tool", ...)` 注入，这是插件化注册的测试注入价值）+ `FakeWeatherTool` 纯逻辑验证工具执行循环，无需网络。
- **测试无网络依赖**：所有用例均为纯逻辑（路由/上下文/指标/工具循环/配置驱动初始化），不需要真实 API key 或本地 Ollama。
- **gRPC 服务端**：需要系统安装 gRPC/Protobuf（`find_package(gRPC CONFIG)` + `grpc_cpp_plugin`），生成代码走 CMake 自定义命令到 `build/sdk/generated/`。`grpc_server.cc` include 的是 `llm_bridge.pb.h`（依赖 include 路径，不再是 `../include/`）。
- **修复的存量 bug**：`DataManager::getSession` 原来查 `session`（单数）表，已改为 `sessions`；`getAllSessions` 里 `ino64_t` 类型错误已改为 `sqlite3_column_int64`。
- **运行时产物**：SQLite 数据库 `chatDB.db` 生成在工作目录，已 gitignore。
