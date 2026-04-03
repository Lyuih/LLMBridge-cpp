#include "../include/DataManager.h"
#include "../include/logger.h"

namespace chat_sdk
{
    DataManager::DataManager(const std::string &dbName)
        : db_(nullptr),
          db_name_(dbName)
    {
        // 打开没有则创建数据库
        if (sqlite3_open(db_name_.c_str(), &db_) != SQLITE_OK)
        {
            LOG_ERROR("数据库连接失败:{}", sqlite3_errmsg(db_));
        }
        LOG_INFO("数据库连接成功{}", db_name_);
        // 初始化数据库
        if (!initDatabase())
        {
            sqlite3_close(db_);
            db_ = nullptr;
            LOG_ERROR("数据库初始化失败");
        }
    }
    DataManager::~DataManager()
    {
        if (db_)
        {
            sqlite3_close(db_);
            db_ = nullptr;
            LOG_INFO("数据库关闭{}", db_name_);
        }
    }
    ///////////////////////////         session相关操作         ///////////////////////////

    bool DataManager::insertSession(const Session &session)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //? 预处理占位符 防止注入
        const std::string sql = "INSERT INTO sessions (session_id,model_name,create_time,update_time) "
                                "VALUES (?,?,?,?);";
        // 编译好的 SQL 语句容器
        sqlite3_stmt *stmt = nullptr;
        // 编译（预处理）带？占位符的 SQL 语句
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, session.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, session.model_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(session.create_at));
        sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(session.updated_at));
        // 执行预编译 SQL 的函数
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt); // 释放stmt
            LOG_ERROR("插入session失败:{}", session.id);
            return false;
        }
        sqlite3_finalize(stmt); // 释放stmt
        LOG_INFO("插入session成功:{}", session.id);
        return true;
    }
    // 获取指定会话
    std::shared_ptr<Session> DataManager::getSession(const std::string &session_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string sql = "SELECT model_name,create_time,update_time FROM session WHERE session_id = ?;";
        // 编译好的 SQL 语句容器
        sqlite3_stmt *stmt = nullptr;
        // 编译（预处理）带？占位符的 SQL 语句
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return nullptr;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

        // 执行sql
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW)
        {
            sqlite3_finalize(stmt); // 释放stmt
            return nullptr;
        }

        // 构建session对象,从当前查询结果行中，取出「第 1 列」的「字符串文本数据」
        std::string model_name(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        auto session = std::make_shared<Session>(model_name);
        session->id = session_id;
        session->create_at = static_cast<std::time_t>(sqlite3_column_int64(stmt, 1));
        session->updated_at = static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
        sqlite3_finalize(stmt); // 释放stmt
        session->messages = getMessageBySessionId(session_id);
        return session;
    }
    // 更新指定会话时间戳
    void DataManager::updateSessionTimestamp(const std::string &session_id, std::time_t timestamp)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string sql = "UPDATE sessions SET update_time = ? WHERE session_id = ?;";

        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return;
        }
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(timestamp));
        sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("更新会话时间戳失败:{}", session_id);
            sqlite3_finalize(stmt); // 释放stmt
            return;
        }
        sqlite3_finalize(stmt); // 释放stmt
        LOG_INFO("更新会话时间戳成功:{}", session_id);
    }
    // 删除指定会话
    bool DataManager::deleteSession(const std::string &session_id)
    {
        /**
         * 先删除会话中的所有信息,再删除会话列表
         * 否则,函数中已经锁了,在未退出deleteMessageBySessionId前会导致死锁
         */
        deleteMessageBySessionId(session_id);
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string sql = "DELETE FROM sessions WHERE session_id = ?;";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("删除会话失败:{}", session_id);
            sqlite3_finalize(stmt); // 释放stmt
            return false;
        }
        sqlite3_finalize(stmt); // 释放stmt
        LOG_INFO("删除会话成功:{}", session_id);
        return true;
    }
    // 获取所有会话id
    std::vector<std::string> DataManager::getAllSessionIds() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> session_ids;
        const std::string sql = "SELECT session_id FROM sessions ORDER BY update_time DESC;";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return {};
        }
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            session_ids.push_back(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt); // 释放stmt
        return session_ids;
    }
    // 获取所有会话信息
    std::vector<std::shared_ptr<Session>> DataManager::getAllSessions() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<Session>> sessions;
        const std::string sql = "SELECT session_id,model_name,create_time,update_time FROM sessions ORDER BY update_time DESC;";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return {};
        }
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            std::string session_id(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
            std::string model_name(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
            int64_t create_time(reinterpret_cast<std::time_t>(sqlite3_column_text(stmt, 2)));
            ino64_t update_time(reinterpret_cast<std::time_t>(sqlite3_column_text(stmt, 3)));

            auto session = std::make_shared<Session>(model_name);
            session->id = session_id;
            session->create_at = create_time;
            session->updated_at = update_time;

            sessions.push_back(session);
        }
        sqlite3_finalize(stmt); // 释放stmt
        return sessions;
    }
    // 删除所有会话
    bool DataManager::clearAllSessions()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string sql = "DELETE FROM sessions";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return {};
        }
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt); // 释放stmt
            LOG_ERROR("删除所有会话失败");
            return false;
        }
        sqlite3_finalize(stmt); // 释放stmt
        LOG_INFO("删除所有会话成功");
        return true;
    }
    // 获取所有会话个数
    size_t DataManager::getSessionCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string sql = "SELECT COUNT(*) FROM sessions";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return 0;
        }
        rc = sqlite3_step(stmt);
        size_t count = 0;
        if (rc == SQLITE_ROW)
        {
            count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt); // 释放stmt
        LOG_INFO("获取所有会话个数:{}", count);
        return count;
    }

    /////////////////////////////  Message相关操作  ///////////////////////////

    // 添加消息
    bool DataManager::insertMessage(const std::string &session_id, const Message &message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string sql = "INSERT INTO messages (message_id,session_id,role,content,timestamp)"
                                "VALUES (?,?,?,?,?);";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_bind_text(stmt, 1, message.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, message.role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, message.content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(message.timestamp));

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt); // 释放stmt
            LOG_ERROR("添加消息失败:{}", sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_finalize(stmt); // 释放stmt

        // 更新session
        const std::string update_sql = "UPDATE sessions SET update_time=? WHERE session_id = ?;";
        sqlite3_stmt *update_stmt = nullptr;
        rc = sqlite3_prepare_v2(db_, update_sql.c_str(), -1, &update_stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_bind_int64(update_stmt, 1, static_cast<int64_t>(message.timestamp));
        sqlite3_bind_text(update_stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(update_stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(update_stmt); // 释放stmt

            LOG_ERROR("更新session失败:{}", sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_finalize(update_stmt);
        return true;
    }
    // 获取指定会话历史消息
    std::vector<Message> DataManager::getMessageBySessionId(const std::string &session_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Message> messages;
        const std::string sql = "SELECT message_id,role,content,timestamp FROM messages WHERE session_id = ? ORDER BY timestamp ASC";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return {};
        }
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            std::string message_id(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
            std::string message_role(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
            std::string message_content(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)));
            int64_t message_timestamp(static_cast<int64_t>(sqlite3_column_int64(stmt, 3)));

            Message message(message_role, message_content);
            message.id = message_id;
            message.timestamp = message_timestamp;
            messages.push_back(message);
        }
        sqlite3_finalize(stmt); // 释放stmt
        return messages;
    }
    // 删除指定会话历史
    bool DataManager::deleteMessageBySessionId(const std::string &session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string sql = "DELETE FROM messages WHERE session_id = ?;";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("stmt绑定失败:{}", sqlite3_errmsg(db_));
            return {};
        }
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt); // 释放stmt

            LOG_ERROR("删除指定会话历史失败:{}", sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_finalize(stmt);
        return true;
    }

    // 初始化数据库
    bool DataManager::initDatabase()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 1.创建session表
        const std::string createSessionsTable = "CREATE TABLE IF NOT EXISTS sessions ("
                                                "session_id TEXT PRIMARY KEY,"
                                                "model_name TEXT NOT NULL,"
                                                "create_time INTEGER NOT NULL,"
                                                "update_time INTEGER NOT NULL"
                                                ");";
        if (!executeSQL(createSessionsTable))
        {
            return false;
        }
        // 2.创建message表
        const std::string createMessagesTable = "CREATE TABLE IF NOT EXISTS messages ("
                                                "message_id TEXT PRIMARY KEY,"
                                                "session_id TEXT NOT NULL,"
                                                "role TEXT NOT NULL,"
                                                "content TEXT NOT NULL,"
                                                "timestamp INTEGER NOT NULL,"
                                                "FOREIGN KEY (session_id) REFERENCES sessions (session_id) on DELETE CASCADE"
                                                ");";
        if (!executeSQL(createMessagesTable))
        {
            return false;
        }
        // 3.创建索引加速查询
        const std::string createMessageIndex = "CREATE INDEX IF NOT EXISTS idx_messages_session_id ON messages (session_id);";
        if (!executeSQL(createMessageIndex))
        {
            return false;
        }
        LOG_INFO("数据库初始化成功");
        return true;
    }
    // 执行SQL语句
    bool DataManager::executeSQL(const std::string &sql)
    {
        char *err_msg = nullptr;
        // sqlite3_exec,执行sql,参数3为回调函数(查询时需要写),参数4为回调函数的参数,不支持占位符
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK)
        {
            LOG_ERROR("执行SQL失败:{}", err_msg);
            // sqlite3_exec执行失败时，会自动在堆上申请内存存储错误文字,需要释放
            sqlite3_free(err_msg);
            return false;
        }
        return true;
    }
}