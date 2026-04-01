#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "../include/llm_bridge.pb.h"
#include "../include/llm_bridge.grpc.pb.h"
#include "../include/ChatSDK.h"
#include "../include/common.h"
#include "../include/logger.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;

using namespace llm_bridge;

// 实现服务类
class LLMBridgeServiderImpl final : public LLMBridge::Service
{
public:
    LLMBridgeServiderImpl()
        : llm_bridge_(std::make_shared<chat_sdk::ChatSDK>())
    {
    }
    Status initModels(ServerContext *context, const InitModelsRequest *request, InitModelsResponse *reply) override
    {
        LOG_INFO("收到 initModels 请求 config 数量:{}", request->configs_size());
        if (request->configs_size() == 0)
        {
            return Status(StatusCode::INVALID_ARGUMENT, "Config 列表为空");
        }
        std::vector<std::shared_ptr<chat_sdk::Config>> configs;

        // 遍历请求中的 repeated 字段
        for (int i = 0; i < request->configs_size(); ++i)
        {
            const ModelConfig &config = request->configs(i);
            // 参数检测
            if (config.model_name().empty())
            {
                return Status(StatusCode::INVALID_ARGUMENT, "model_name为空,列表下标:" + std::to_string(i));
            }
            const std::string model_type = config.model_type();
            if (model_type == "ollama")
            {
                auto ollama_config = std::make_shared<chat_sdk::OllamaConfig>();
                ollama_config->model_name = config.model_name();
                ollama_config->model_desc_ = config.model_desc();
                ollama_config->endPoint_ = config.base_url();
                configs.push_back(ollama_config);
            }
            else if (model_type == "api")
            {
                auto api_config = std::make_shared<chat_sdk::ApiConfig>();
                api_config->model_name = config.model_name();
                api_config->model_desc_ = config.model_desc();
                api_config->endPoint_ = config.base_url();
                api_config->api_key = config.api_key();
                configs.push_back(api_config);
            }
            else
            {
                return Status(StatusCode::INVALID_ARGUMENT, "未知类型,列表下标:" + std::to_string(i));
            }
            LOG_INFO("初始化 model:{}", config.model_name());
        }
        bool flag = llm_bridge_->initModels(configs);
        // 设置响应
        if (flag)
        {
            reply->set_success(true);
            reply->set_error("");
        }
        else
        {
            reply->set_success(false);
            reply->set_error("SDK 初始化部分或者全部模型失败");
        }
        return Status::OK;
    }

    Status createSession(ServerContext *context, const CreateSessionRequest *request,
                         CreateSessionResponse *reply) override
    {
        LOG_INFO("收到 createSession 请求");
        const std::string model_name = request->model_name();
        if (model_name.empty())
        {
            return Status(StatusCode::INVALID_ARGUMENT, "model_name为空");
        }
        const std::string session_id = llm_bridge_->createSession(model_name);
        if (session_id.empty())
        {
            reply->set_error("创建会话失败,模型可能未初始化");
        }
        else
        {
            reply->set_session_id(session_id);
            reply->set_error("");
        }
        return Status::OK;
    }

    Status getSession(ServerContext *context, const SessionIdRequest *request,
                      GetSessionResponse *reply) override
    {
        LOG_INFO("收到 getSession 请求");
        const std::string session_id = request->session_id();
        if (session_id.empty())
        {
            return Status(StatusCode::INVALID_ARGUMENT, "session_id为空");
        }
        auto session_p = llm_bridge_->getSession(session_id);
        if (!session_p)
        {
            return Status(StatusCode::NOT_FOUND, "没有找到对应的Session");
        }
        Session *session_reply = reply->mutable_session();
        session_reply->set_model_name(session_p->model_name);
        session_reply->set_create_at(session_p->create_at);
        session_reply->set_session_id(session_p->id);
        session_reply->set_updated_at(session_p->updated_at);
        for (const auto &msg : session_p->messages)
        {
            Message *message_p = session_reply->add_messages();
            message_p->set_content(msg.content);
            message_p->set_message_id(msg.id);
            message_p->set_role(msg.role);
            message_p->set_timestamp(msg.timestamp);
        }

        return Status::OK;
    }

    // 目前请求为空
    Status getAvailableModels(ServerContext *context, const GetAvailableModelsRequest *request,
                              GetAvailableModelsResponse *reply) override
    {
        LOG_INFO("收到 getAvailableModels 请求");

        std::vector<chat_sdk::ModelInfo> model_infos = llm_bridge_->getAvailableModels();
        for (const chat_sdk::ModelInfo &model_info : model_infos)
        {
            ModelInfo *models = reply->add_models();
            models->set_description(model_info.desc_);
            models->set_endpoint(model_info.endpoint_);
            models->set_model_name(model_info.name_);
            models->set_provider(model_info.provider_);
            models->set_is_init(model_info.isInit_);
        }
        return Status::OK;
    }

    Status deleteSession(ServerContext *context, const SessionIdRequest *request,
                         DeleteSessionResponse *reply) override
    {
        LOG_INFO("收到 deleteSession 请求");
        if (request->session_id().empty())
        {
            return Status(StatusCode::INVALID_ARGUMENT, "session_id为空");
        }

        bool success = llm_bridge_->deleteSession(request->session_id());
        reply->set_success(success);
        if (!success)
        {
            reply->set_error("删除失败，会话可能不存在");
        }
        return Status::OK;
    }

    Status getSessonList(ServerContext *context, const GetSessionListRequest *request,
                         GetSessionListResponse *reply) override
    {
        LOG_INFO("收到 getSessonList 请求");
        std::vector<std::string> session_ids = llm_bridge_->getSessionList();

        for (const std::string &id : session_ids)
        {
            reply->add_session_ids(id);
        }
        return Status::OK;
    }

    Status sendMessage(ServerContext *context, const SendMessageRequest *request, SendMessageResponse *reply) override
    {
        LOG_INFO("收到 sendMessage 请求");
        if (request->session_id().empty() || request->message().empty())
        {
            return Status(StatusCode::INVALID_ARGUMENT, "session_id或message为空");
        }

        // 调用全量回复接口 (由于网络请求可能会报错，建议加异常捕获，取决于你的 SDK 设计)
        std::string response_content = llm_bridge_->sendMessage(request->session_id(), request->message());

        if (response_content.empty())
        {
            reply->set_error("生成回答失败");
        }
        else
        {
            reply->set_chunk(response_content);
            reply->set_is_last(true);
        }

        return Status::OK;
    }

    Status sendMessageStream(ServerContext *context, const SendMessageRequest *request, ServerWriter<SendMessageResponse> *writer) override
    {
        LOG_INFO("收到 sendMessageStream 流式请求");
        if (request->session_id().empty() || request->message().empty())
        {
            return Status(StatusCode::INVALID_ARGUMENT, "session_id或message为空");
        }

        // 构造传给 SDK 的回调函数
        // 假设你的 LLMProvider::func_stream 是类似 std::function<void(const std::string&, bool)> 的结构
        auto callback = [writer](const std::string &chunk_text, bool is_last_chunk)
        {
            SendMessageResponse response;
            response.set_chunk(chunk_text);
            response.set_is_last(is_last_chunk);

            // 将数据块通过 gRPC 流发送给客户端
            writer->Write(response);
        };

        // 调用 SDK 的流式接口 (该函数应该会阻塞，直到所有流输出完毕)
        // 注意：如果你 SDK 里的回调函数没有 is_last 字段，你需要自己在此处维护逻辑
        std::string full_res = llm_bridge_->sendMessageStream(request->session_id(), request->message(), callback);

        return Status::OK;
    }

private:
    std::shared_ptr<chat_sdk::ChatSDK> llm_bridge_;
};
void RunServer()
{
    // 1.设置监听端口
    std::string server_address("0.0.0.0:50051");
    LLMBridgeServiderImpl service;
    ServerBuilder builder;
    //2.监听指定的地址和端口
    builder.AddListeningPort(server_address,grpc::InsecureServerCredentials());

    //注册刚才实现的Service
    builder.RegisterService(&service);

    //3.构建并启动服务器
    std::unique_ptr<Server> server(builder.BuildAndStart());
    LOG_INFO("LLMBrider Server listening on {}",server_address);
    server->Wait();
}

int main()
{
    return 0;
}