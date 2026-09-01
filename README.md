# LLMBridge

一个 C++17 编写的**大模型统一接入 SDK**：以统一的对话接口对接 DeepSeek / OpenAI 兼容（GPT 等）/ Gemini / Ollama 本地模型，并提供配置驱动注册、模型路由与故障转移、上下文 Token 管理、Function Calling 工具调用、请求可观测性等工程能力，可选通过 gRPC 对外提供服务。

## 特性

- 🧩 **配置驱动 + 插件化注册**：JSON 配置加载任意模型，`ProviderFactory` 按类型字符串创建 provider，接入新模型无需改代码
- 🔀 **模型路由 / 故障转移**：加权路由分配流量，失败自动熔断 + fallback 切换，超时自动恢复
- 📐 **上下文 / Token 管理**：token 估算、按预算裁剪历史、溢出摘要保留上下文
- 🛠️ **Function Calling**：工具 schema 注入、模型调工具、执行结果回填，多轮循环
- 📊 **可观测性**：请求耗时 / token 消耗 / 错误率统计，JSON 导出
- 🧵 **会话持久化**：SQLite 存储会话与消息，内存缓存 + 预编译语句防注入
- 🔌 **gRPC 服务**：protobuf 定义，流式响应支持（可选构建）

## 架构

```
                     ┌─────────────────────────────────────┐
  客户端/CLI/测试     │              ChatSDK (门面)          │
  ───────────────▶   │  initFromConfigFile ── ConfigLoader │
                     │  sendMessage ── Router(加权+熔断)     │
                     │             ├─ ContextManager(裁剪)   │
                     │             ├─ ToolRegistry(工具循环)  │
                     │             └─ MetricsCollector(指标)  │
                     │  LLMManager ── LLMProvider (多态)      │
                     │    DeepSeek / GPT / Gemini / Ollama   │
                     │  SessionManager ── DataManager(SQLite)│
                     └─────────────────────────────────────┘
```

## 构建

依赖全部内嵌在 `third_party/`，无需系统安装。gRPC 服务端为可选目标。

```bash
cmake -B build -S .
cmake --build build

# 单元测试(排除需要本地 Ollama 的网络用例)
./build/Debug/LLMTest.exe --gtest_filter=-OllamaLLMProviderTest.*

# CLI demo
./build/Debug/cli_demo.exe config/models.example.json
```

## 配置驱动示例

`config/models.example.json`：

```json
{
  "models": [
    {
      "name": "deepseek-chat",
      "provider": "deepseek",
      "api_key": "sk-xxx",
      "base_url": "https://api.deepseek.com",
      "temperature": 0.7,
      "max_tokens": 2048,
      "weight": 3,
      "fallback": ["gpt-4o-mini", "gemini-2.5-flash-lite"]
    },
    {
      "name": "smart",                 // 虚拟路由组: 按权重在 deepseek/gpt 间分配流量
      "route": ["deepseek-chat", "gpt-4o-mini"]
    }
  ]
}
```

字段说明：
- `provider`: provider 类型（`deepseek` / `gpt` / `gemini` / `ollama`，可注册自定义类型）
- `weight`: 路由权重；`fallback`: 故障转移链；`route`: 虚拟路由组
- `context_window`: 上下文窗口（token），启用后自动裁剪历史

## 使用（SDK）

```cpp
auto sdk = std::make_shared<chat_sdk::ChatSDK>();
sdk->initFromConfigFile("config/models.example.json");  // 配置驱动初始化

std::string session_id = sdk->createSession("deepseek-chat");
std::string reply = sdk->sendMessage(session_id, "你好");

// Function Calling
sdk->registerTool(std::make_shared<MyCalculatorTool>());
// ... sendMessage 时模型可自动调用工具并拿到结果

// 可观测性
std::string metrics = sdk->getMetricsJson();
```

## 目录结构

```
sdk/            核心 SDK(include/ 头文件, src/ 实现, proto/ gRPC 定义)
test/           gtest 单元测试(纯逻辑用例无需网络)
demo/           CLI 交互 demo
config/         配置驱动示例
third_party/    内嵌依赖(fmt/spdlog/jsoncpp/sqlite3/googletest)
```

## 技术栈

C++17 · CMake · gRPC/Protobuf · SQLite · cpp-httplib · jsoncpp · spdlog · gtest
