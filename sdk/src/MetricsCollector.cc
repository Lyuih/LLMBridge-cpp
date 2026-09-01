#include <sstream>
#include "../include/MetricsCollector.h"
#include "../include/logger.h"

namespace chat_sdk
{
    MetricsCollector &MetricsCollector::instance()
    {
        static MetricsCollector collector;
        return collector;
    }

    void MetricsCollector::record(const std::string &model_name, double latency_ms, bool success,
                                  int input_tokens, int output_tokens)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &m = metrics_[model_name];
        m.request_count++;
        if (success)
        {
            m.success_count++;
        }
        else
        {
            m.error_count++;
        }
        m.total_latency_ms += latency_ms;
        m.total_input_tokens += input_tokens;
        m.total_output_tokens += output_tokens;
    }

    void MetricsCollector::recordTokens(const std::string &model_name, int input_tokens, int output_tokens)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &m = metrics_[model_name];
        m.total_input_tokens += input_tokens;
        m.total_output_tokens += output_tokens;
    }

    ModelMetrics MetricsCollector::getModelMetrics(const std::string &model_name) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(model_name);
        return it == metrics_.end() ? ModelMetrics{} : it->second;
    }

    std::map<std::string, ModelMetrics> MetricsCollector::getAll() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return metrics_;
    }

    void MetricsCollector::reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.clear();
        LOG_INFO("metrics 已清空");
    }

    std::string MetricsCollector::toJson() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream os;
        os << "{";
        bool first = true;
        for (const auto &pair : metrics_)
        {
            if (!first)
            {
                os << ",";
            }
            first = false;
            const auto &m = pair.second;
            os << "\"" << pair.first << "\":{"
               << "\"request_count\":" << m.request_count << ","
               << "\"success_count\":" << m.success_count << ","
               << "\"error_count\":" << m.error_count << ","
               << "\"avg_latency_ms\":" << m.avgLatencyMs() << ","
               << "\"error_rate\":" << m.errorRate() << ","
               << "\"input_tokens\":" << m.total_input_tokens << ","
               << "\"output_tokens\":" << m.total_output_tokens << "}";
        }
        os << "}";
        return os.str();
    }
}
