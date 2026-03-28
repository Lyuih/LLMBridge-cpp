/**
 * @file SessionManager.h
 * @author yui
 */

#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include "common.h"

namespace chat_sdk
{
    class SessionManager
    {
    public:
        // 创建新会话
        std::string createSession(const std::string &model_name);
        // 获取具体会话,通过会话id获取,返回会话指针
        std::shared_ptr<Session> getSession(const std::string &session_id);
        // 添加消息到会话
        bool addMessage(const std::string &session_id, const std::string &role, const std::string &content);
        // 获取会话历史信息
        std::vector<Message> getSessionHistory(const std::string &session_id);
        // 获取会话列表
        std::vector<std::string> getSessionList() const;
        // 删除会话
        bool deleteSession(const std::string &session_id);
        // 更新会话时间戳
        void updateSessionTimestamp(const std::string &session_id);
        // 清空所有会话
        void clearAllSession();
        // 获取会话总数
        size_t getSessionCount() const;

    private:
        // 生成唯一会话id
        std::string generateSessionId();
        // 生成唯一消息id
        std::string generateMessageId();

    private:
        std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
        mutable std::mutex mutex_;
        static std::atomic<int64_t> message_counter_;
        static std::atomic<int64_t> session_counter_;
    };
    std::atomic<int64_t> SessionManager::message_counter_{0};
    std::atomic<int64_t> SessionManager::session_counter_{0};
}
#endif