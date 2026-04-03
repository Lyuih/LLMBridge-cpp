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
    // std::atomic<int64_t> SessionManager::message_counter_{0};
    // std::atomic<int64_t> SessionManager::session_counter_{0};
    SessionManager::SessionManager()
        : dataManager_("chatDB.db")
    {
        // 获取所有会话
        auto sessions = dataManager_.getAllSessions();
        for (auto &session : sessions)
        {
            session->messages = dataManager_.getMessageBySessionId(session->id);
            sessions_[session->id] = session;
        }
        LOG_INFO("会话管理初始化完成,当前会话数:{}", sessions.size());
    }
    // 创建新会话
    std::string SessionManager::createSession(const std::string &model_name)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 1.生成会话id
        std::string session_id = generateSessionId();
        // 2.创建会话
        auto session = std::make_shared<Session>(model_name);
        session->id = session_id;
        // 3.加入会话列表
        sessions_.emplace(session_id, session);
        LOG_INFO("创建新会话成功session_id:{}model_name{}", session_id, model_name);
        lock.unlock();

        // 数据持久化
        dataManager_.insertSession(*session);
        return session_id;
    }
    // 获取具体会话,通过会话id获取,返回会话指针
    std::shared_ptr<Session> SessionManager::getSession(const std::string &session_id)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = sessions_.find(session_id);
            if (it != sessions_.end())
            {
                LOG_INFO("获取内存中会话成功:{}", session_id);
                return it->second;
            }
        }

        // 内存中没有,去数据库看看
        auto session = dataManager_.getSession(session_id);
        std::unique_lock<std::mutex> lock(mutex_);
        if (session == nullptr)
        {
            LOG_ERROR("会话不存在:{}", session_id);
            return nullptr;
        }
        // 加入内存
        sessions_.emplace(session_id, session);
        lock.unlock();
        // 获取会话历史数据
        session->messages = dataManager_.getMessageBySessionId(session_id);
        LOG_INFO("获取数据库中会话成功:{}", session_id);
        return session;
    }
    // 添加消息到会话
    bool SessionManager::addMessage(const std::string &session_id, Message&msg)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            LOG_ERROR("添加消息到会话失败,没有该会话{}", session_id);
            return false;
        }
        // Message msg(role, content);
        msg.id = generateMessageId();
        it->second->messages.push_back(msg);
        lock.unlock();
        updateSessionTimestamp(session_id);
        LOG_INFO("添加消息到会话中:{}:{}", session_id, msg.content);
        dataManager_.insertMessage(session_id, msg);
        return true;
    }
    // 获取会话历史信息
    std::vector<Message> SessionManager::getSessionHistory(const std::string &session_id)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            return it->second->messages;

            // LOG_ERROR("获取会话历史信息失败,没有该会话{}", session_id);
            // return std::vector<Message>();
        }
        lock.unlock();
        return dataManager_.getMessageBySessionId(session_id);
    }
    // 获取会话列表
    std::vector<std::string> SessionManager::getSessionList() const
    {
        auto sessions = dataManager_.getAllSessions();
        std::unique_lock<std::mutex> lock(mutex_);
        // 获取sessions中的所有session_id,对其最近更新时间排降序
        std::vector<std::pair<std::time_t, std::shared_ptr<const Session>>> temp;
        temp.reserve(sessions_.size() + sessions.size());
        for (const auto &pair : sessions_)
        {
            const auto &session_ptr = pair.second;
            temp.emplace_back(session_ptr->updated_at, session_ptr);
        }
        for (const auto &session : sessions)
        {
            if (sessions_.find(session->id) == sessions_.end())
            {
                temp.emplace_back(session->updated_at, session);
            }
        }

        std::sort(temp.begin(), temp.end(), [](const auto &a, const auto &b)
                  { return a.first > b.first; });
        std::vector<std::string> session_ids;
        session_ids.reserve(temp.size());

        for (const auto &item : temp)
        {
            session_ids.push_back(item.second->id);
        }
        return session_ids;
    }
    // 删除会话
    bool SessionManager::deleteSession(const std::string &session_id)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            LOG_ERROR("删除会话失败,没有该会话{}", session_id);
            return false;
        }
        LOG_INFO("删除会话{}", session_id);
        sessions_.erase(it);
        lock.unlock();
        dataManager_.deleteSession(session_id);
        return true;
    }
    // 更新会话时间戳
    void SessionManager::updateSessionTimestamp(const std::string &session_id)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            LOG_ERROR("更新会话失败,没有该会话{}", session_id);
            return;
        }
        it->second->update();
        // lock.unlock();
        // dataManager_.updateSessionTimestamp(session_id, it->second->updated_at);
        return;
    }
    // 清空所有会话
    void SessionManager::clearAllSession()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        LOG_INFO("删除{}条会话", sessions_.size());
        sessions_.clear();
        lock.unlock();
        dataManager_.clearAllSessions();
        return;
    }
    // 获取会话总数
    size_t SessionManager::getSessionCount() const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return sessions_.size();
    }

    // 生成唯一会话id
    std::string SessionManager::generateSessionId()
    {
        return generate_uuid_v4();
    }
    // 生成唯一消息id
    std::string SessionManager::generateMessageId()
    {
        return generate_uuid_v4();
    }
}