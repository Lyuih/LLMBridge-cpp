/**
 * @file MetricsCollector.h
 * @author yui
 */

// 可观测性: 请求耗时 / token 消耗 / 错误率 统计
// 按模型维度聚合,提供查询接口与 JSON 导出,供 gRPC/CLI 展示

#ifndef METRICSCOLLECTOR_H
#define METRICSCOLLECTOR_H

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace chat_sdk
{
    // 单个模型维度的指标
    struct ModelMetrics
    {
        int64_t request_count = 0;    // 总请求数
        int64_t success_count = 0;    // 成功数
        int64_t error_count = 0;      // 失败数
        double total_latency_ms = 0;  // 累计耗时
        int64_t total_input_tokens = 0;  // 累计输入 token
        int64_t total_output_tokens = 0; // 累计输出 token

        // 平均耗时(ms)
        double avgLatencyMs() const
        {
            return request_count == 0 ? 0.0 : total_latency_ms / request_count;
        }
        // 错误率 [0,1]
        double errorRate() const
        {
            return request_count == 0 ? 0.0 : static_cast<double>(error_count) / request_count;
        }
        // 是否健康(错误率阈值判断,用于路由层健康提示)
        bool healthy(double threshold = 0.5) const
        {
            return request_count == 0 || errorRate() < threshold;
        }
    };

    class MetricsCollector
    {
    public:
        static MetricsCollector &instance();

        // 记录一次请求结果: 模型名, 耗时ms, 是否成功, 输入/输出 token
        void record(const std::string &model_name, double latency_ms, bool success,
                    int input_tokens = 0, int output_tokens = 0);
        // 单独记录 token 消耗(如从响应 usage 字段解析时)
        void recordTokens(const std::string &model_name, int input_tokens, int output_tokens);

        // 查询单个模型指标(不存在则返回全 0)
        ModelMetrics getModelMetrics(const std::string &model_name) const;
        // 查询全部指标
        std::map<std::string, ModelMetrics> getAll() const;
        // 导出为 JSON 字符串,便于接口/CLI 展示
        std::string toJson() const;
        // 清空所有指标
        void reset();

    private:
        MetricsCollector() = default;
        std::map<std::string, ModelMetrics> metrics_;
        mutable std::mutex mutex_;
    };
}

#endif