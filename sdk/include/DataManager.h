/**
 * @file DataManager.h
 * @author yui
 */

#ifndef DATAMANAGER_H
#define DATAMANAGER_H
#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <mutex>
#include "common.h"

namespace chat_sdk
{
    class DataManager
    {
    public:
        DataManager(const std::string &dbName);
        ~DataManager();
        // session相关操作
        bool insertSession(const Session &session);
        // 获取指定会话
        std::shared_ptr<Session> getSession(const std::string &session_id) const;
        // 更新指定会话时间戳
        void updateSessionTimestamp(const std::string &session_id, std::time_t timestamp);
        // 删除指定会话
        bool deleteSession(const std::string &session_id);
        // 获取所有会话id
        std::vector<std::string> getAllSessionIds() const;
        // 获取所有会话信息
        std::vector<std::shared_ptr<Session>> getAllSessions() const;
        // 删除所有会话
        bool clearAllSessions();
        // 获取所有会话个数
        size_t getSessionCount() const;

        // Message相关操作
        bool insertMessage(const std::string &session_id, const Message &message);
        // 获取指定会话历史消息
        std::vector<Message> getMessageBySessionId(const std::string &session_id) const;
        // 删除指定会话历史
        bool deleteMessageBySessionId(const std::string &session_id);

    private:
        // 初始化数据库
        bool initDatabase();
        // 执行SQL语句
        bool executeSQL(const std::string &sql);

    private:
        sqlite3 *db_;
        std::string db_name_;
        mutable std::mutex mutex_;
    };
}

#endif