#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include "../include/Router.h"
#include "../include/logger.h"

namespace chat_sdk
{
    Router::Router()
        : rng_(std::random_device{}())
    {
    }

    void Router::addBackend(const std::string &model_name, int weight, const std::vector<std::string> &fallback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &info = backends_[model_name];
        info.weight = std::max(0, weight);
        info.fallback = fallback;
        if (info.fail_count > 0)
        {
            info.fail_count = 0;
            info.fail_time = 0;
        }
        LOG_INFO("路由:注册后端 {} weight:{}", model_name, info.weight);
    }

    void Router::addRoute(const std::string &route_name, const std::vector<std::string> &backends)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (backends.empty())
        {
            LOG_WARN("路由:{} 后端列表为空,忽略", route_name);
            return;
        }
        routes_[route_name] = backends;
        LOG_INFO("路由:注册路由组 {} -> {} 个后端", route_name, backends.size());
    }

    std::vector<std::string> Router::plan(const std::string &model_name)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. 确定候选后端集合: 路由组或单模型
        std::vector<std::string> backends;
        auto it = routes_.find(model_name);
        if (it != routes_.end())
        {
            backends = it->second;
        }
        else
        {
            backends = {model_name};
        }

        // 2. 计算尝试顺序(加权随机主选 + 权重降序备选 + 熔断兜底 + fallback 链)
        std::vector<std::string> order = buildOrderLocked(backends);

        // 3. 追加每个候选的 fallback 链
        for (const auto &name : order)
        {
            auto bit = backends_.find(name);
            if (bit == backends_.end())
            {
                continue;
            }
            for (const auto &fb : bit->second.fallback)
            {
                if (std::find(order.begin(), order.end(), fb) == order.end())
                {
                    order.push_back(fb);
                }
            }
        }

        if (order.size() > 1)
        {
            LOG_DEBUG("路由:{} 尝试顺序:{}", model_name, [&]() {
                std::string s;
                for (const auto &n : order)
                {
                    s += n + " -> ";
                }
                return s;
            }());
        }
        return order;
    }

    std::vector<std::string> Router::buildOrderLocked(const std::vector<std::string> &backends) const
    {
        // 拆分健康与熔断
        std::vector<std::pair<std::string, int>> healthy; // (name, weight)
        std::vector<std::string> down;
        int total_weight = 0;
        for (const auto &name : backends)
        {
            auto it = backends_.find(name);
            if (it == backends_.end())
            {
                continue; // 未注册的后端跳过
            }
            if (isHealthyLocked(it->second) && it->second.weight > 0)
            {
                healthy.push_back({name, it->second.weight});
                total_weight += it->second.weight;
            }
            else
            {
                down.push_back(name);
            }
        }

        std::vector<std::string> order;
        order.reserve(healthy.size() + down.size());

        // 加权随机选主目标
        if (total_weight > 0)
        {
            int r = static_cast<int>(rng_() % total_weight);
            int acc = 0;
            auto pick_it = healthy.begin();
            for (auto it2 = healthy.begin(); it2 != healthy.end(); ++it2)
            {
                acc += it2->second;
                if (r < acc)
                {
                    pick_it = it2;
                    break;
                }
            }
            order.push_back(pick_it->first);
        }

        // 其余健康后端按权重降序
        std::vector<std::pair<std::string, int>> rest;
        const std::string primary = order.empty() ? "" : order.front();
        for (const auto &p : healthy)
        {
            if (p.first != primary)
            {
                rest.push_back(p);
            }
        }
        std::sort(rest.begin(), rest.end(), [](const auto &a, const auto &b)
                  { return a.second > b.second; });
        for (const auto &p : rest)
        {
            order.push_back(p.first);
        }

        // 熔断中的后端兜底
        for (const auto &name : down)
        {
            order.push_back(name);
        }

        return order;
    }

    void Router::markUnavailable(const std::string &model_name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backends_.find(model_name);
        if (it == backends_.end())
        {
            return;
        }
        it->second.fail_count++;
        it->second.fail_time = std::time(nullptr);
        LOG_WARN("路由:{} 熔断,连续失败 {} 次", model_name, it->second.fail_count);
    }

    void Router::markAvailable(const std::string &model_name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backends_.find(model_name);
        if (it == backends_.end())
        {
            return;
        }
        it->second.fail_count = 0;
        it->second.fail_time = 0;
        LOG_INFO("路由:{} 手动恢复", model_name);
    }

    bool Router::isHealthy(const std::string &model_name) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backends_.find(model_name);
        if (it == backends_.end())
        {
            return false;
        }
        return isHealthyLocked(it->second);
    }

    bool Router::isHealthyLocked(const BackendInfo &info) const
    {
        if (info.fail_count == 0)
        {
            return true;
        }
        // 熔断窗口: 超时自动恢复(半开)
        auto now = std::time(nullptr);
        return (now - info.fail_time) * 1000 > recover_after_ms_;
    }

    void Router::setRecoverAfterMs(int ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recover_after_ms_ = std::max(0, ms);
    }

    std::vector<std::string> Router::routeNames() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(routes_.size());
        for (const auto &pair : routes_)
        {
            names.push_back(pair.first);
        }
        return names;
    }
}
