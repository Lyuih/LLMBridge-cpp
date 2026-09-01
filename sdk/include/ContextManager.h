/**
 * @file ContextManager.h
 * @author yui
 */

// 上下文 / Token 管理
// - Token 估算: 字符比例启发式(中文≈0.7 token/字符, 英文≈0.25 token/字符)
// - 预算裁剪: 超预算时保留最近消息,把溢出的历史压缩成摘要前缀,避免丢上下文
// 解决"把完整历史全量发给模型导致超长"的工程问题

#ifndef CONTEXTMANAGER_H
#define CONTEXTMANAGER_H

#include <string>
#include <vector>
#include "common.h"

namespace chat_sdk
{
    class ContextManager
    {
    public:
        // 估算一段文本的 token 数(启发式,不依赖真实 tokenizer)
        static int estimateTokens(const std::string &text);
        // 计算一组消息的 token 总量(含 role 等格式开销)
        int countMessagesTokens(const std::vector<Message> &messages) const;
        // 裁剪消息到 token 预算: 保留最近对话,溢出的历史摘要为 system 前缀
        // budget <= 0 表示不裁剪
        std::vector<Message> fitToBudget(const std::vector<Message> &messages, int budget) const;
        // 把对话历史压缩成一段摘要文本(预算内,保留头部与尾部)
        static std::string summarizeConversation(const std::vector<Message> &messages, int budget);
    };
}

#endif