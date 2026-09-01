#include <cmath>
#include <algorithm>
#include "../include/ContextManager.h"
#include "../include/logger.h"

namespace chat_sdk
{
    // 每个消息的格式开销(role + 分隔符 + 轮次标记)
    static constexpr int MESSAGE_OVERHEAD_TOKENS = 4;

    int ContextManager::estimateTokens(const std::string &text)
    {
        int cjk = 0;
        int ascii = 0;
        size_t i = 0;
        const size_t n = text.size();
        while (i < n)
        {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x80)
            {
                ascii++;
                i++;
            }
            else
            {
                int len = 1;
                if ((c & 0xE0) == 0xC0)
                {
                    len = 2;
                }
                else if ((c & 0xF0) == 0xE0)
                {
                    len = 3;
                }
                else if ((c & 0xF8) == 0xF0)
                {
                    len = 4;
                }
                else
                {
                    i++;
                    continue;
                }
                cjk++; // 多字节字符按 CJK 密计算
                i += len;
            }
        }
        // 中文约 1 字符 ≈ 0.7 token;英文约 4 字符 = 1 token
        return static_cast<int>(std::ceil(cjk * 0.7 + ascii * 0.25));
    }

    int ContextManager::countMessagesTokens(const std::vector<Message> &messages) const
    {
        int total = 0;
        for (const auto &m : messages)
        {
            total += estimateTokens(m.content) + MESSAGE_OVERHEAD_TOKENS;
        }
        return total;
    }

    std::vector<Message> ContextManager::fitToBudget(const std::vector<Message> &messages, int budget) const
    {
        if (budget <= 0 || messages.empty())
        {
            return messages;
        }
        if (countMessagesTokens(messages) <= budget)
        {
            return messages;
        }

        std::vector<Message> result;
        int remaining = budget;

        // 从最新消息往回收集放得下的消息,保证最近的上下文优先保留
        for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i)
        {
            const Message &m = messages[i];
            int cost = estimateTokens(m.content) + MESSAGE_OVERHEAD_TOKENS;
            if (remaining - cost >= 0)
            {
                result.insert(result.begin(), m);
                remaining -= cost;
            }
            else
            {
                // 最新一条也放不下时,对最后一条用户消息做硬截断,保证有响应可依赖
                if (result.empty() && m.role == "user" && remaining > MESSAGE_OVERHEAD_TOKENS)
                {
                    Message truncated = m;
                    int chars_budget = remaining - MESSAGE_OVERHEAD_TOKENS;
                    truncated.content = m.content.substr(0, static_cast<size_t>(chars_budget * 3));
                    if (!truncated.content.empty())
                    {
                        result.insert(result.begin(), truncated);
                    }
                }
                break;
            }
        }

        // 溢出的旧历史压缩成摘要,作为 system 前缀保留全局上下文
        size_t kept = result.size();
        std::vector<Message> overflow(messages.begin(), messages.begin() + (messages.size() - kept));
        if (!overflow.empty())
        {
            std::string summary = summarizeConversation(overflow, 80);
            if (!summary.empty())
            {
                Message sys("system", "[历史对话摘要] " + summary);
                result.insert(result.begin(), sys);
            }
        }

        LOG_WARN("上下文裁剪: {} 条消息 -> {} 条(预算 {} token)", messages.size(), result.size(), budget);
        return result;
    }

    std::string ContextManager::summarizeConversation(const std::vector<Message> &messages, int budget)
    {
        std::string joined;
        for (const auto &m : messages)
        {
            joined += m.role + ":" + m.content + "\n";
        }
        if (joined.empty())
        {
            return "";
        }
        if (estimateTokens(joined) <= budget)
        {
            return joined;
        }
        // 超出预算: 保留头部与尾部,中间省略(真实场景可替换为调用模型做摘要)
        int half_chars = static_cast<int>(budget * 1.5);
        std::string head = joined.substr(0, static_cast<size_t>(std::max(0, half_chars / 2)));
        std::string tail = joined.substr(joined.size() - static_cast<size_t>(std::max(0, half_chars / 2)));
        return head + "\n...[中略]...\n" + tail;
    }
}
