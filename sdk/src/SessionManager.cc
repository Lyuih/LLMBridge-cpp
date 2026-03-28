#include <iostream>
#include <sstream>
#include <iomanip>

#include "../include/SessionManager.h"
#include "../include/logger.h"
#include "../include/fields.h"

/**
        std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
        mutable std::mutex mutex_;
        static std::atomic<int64_t> message_counter_;
        static std::atomic<int64_t> session_counter_;
 */
namespace chat_sdk
{
    // 创建新会话
    std::string SessionManager::createSession(const std::string &model_name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 1.生成会话id
        std::string session_id = generateSessionId();
        // 2.创建会话
        auto session = std::make_shared<Session>(model_name);
        session->id = session_id;
        // 3.加入会话列表
        sessions_.emplace(session_id, session);
        LOG_INFO("创建新会话成功session_id:{}model_name{}", session_id, model_name);
        return session_id;
    }
    // 获取具体会话,通过会话id获取,返回会话指针
    std::shared_ptr<Session> SessionManager::getSession(const std::string &session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            return nullptr;
        }
        return it->second;
    }
    // 添加消息到会话
    bool SessionManager::addMessage(const std::string &session_id, const std::string &role, const std::string &content)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            LOG_ERROR("添加消息到会话失败,没有该会话{}", session_id);
            return false;
        }
        Message msg(role, content);
        msg.id = generateMessageId();
        it->second->messages.push_back(std::move(msg));
        updateSessionTimestamp(session_id);
        LOG_INFO("添加消息到会话中:{}:{}", session_id, msg.content);
        return true;
    }
    // 获取会话历史信息
    std::vector<Message> SessionManager::getSessionHistory(const std::string &session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            LOG_ERROR("获取会话历史信息失败,没有该会话{}", session_id);
            return std::vector<Message>();
        }
        return it->second->messages;
    }
    // 获取会话列表
    std::vector<std::string> SessionManager::getSessionList() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 获取sessions中的所有session_id,对其最近更新时间排降序
        std::vector<std::pair<std::time_t, std::shared_ptr<const Session>>> temp;
        temp.reserve(sessions_.size());
        for (const auto &pair : sessions_)
        {
            const auto &session_ptr = pair.second;
            temp.emplace_back(session_ptr->updated_at, session_ptr);
        }
        std::sort(temp.begin(), temp.end(), [](const auto &a, const auto &b)
                  { return a.first > b.first; });
        std::vector<std::string> session_ids;
        for (const auto &item : temp)
        {
            session_ids.push_back(item.second->id);
        }
        return session_ids;
    }
    // 删除会话
    bool SessionManager::deleteSession(const std::string &session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            LOG_ERROR("删除会话失败,没有该会话{}", session_id);
            return false;
        }
        LOG_INFO("删除会话{}", session_id);
        sessions_.erase(it);
        return true;
    }
    // 更新会话时间戳
    void SessionManager::updateSessionTimestamp(const std::string &session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            LOG_ERROR("更新会话失败,没有该会话{}", session_id);
            return;
        }
        it->second->update();
        return;
    }
    // 清空所有会话
    void SessionManager::clearAllSession()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LOG_INFO("删除{}条会话", sessions_.size());
        sessions_.clear();
    }
    // 获取会话总数
    size_t SessionManager::getSessionCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

    // 生成唯一会话id
    std::string SessionManager::generateSessionId()
    {
        // 会话id格式: session_时间戳_消息计数
        session_counter_.fetch_add(1);
        std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::ostringstream oss;
        oss << "session_" << time << "_" << std::setfill('0') << std::setw(8) << session_counter_;
    }
    // 生成唯一消息id
    std::string SessionManager::generateMessageId()
    {
        // 会话id格式: msg_时间戳_消息计数
        message_counter_.fetch_add(1);
        std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::ostringstream oss;
        oss << "msg_" << time << "_" << std::setfill('0') << std::setw(8) << message_counter_;
    }
}