/**
 * @file Router.h
 * @author yui
 */

// 模型路由 / 故障转移
// - 加权路由: 一个虚拟路由组名可指向多个后端模型,按各自 weight 随机分配流量
// - 熔断: 后端调用失败进入熔断窗口,期间不被选中;超时后自动恢复(半开探测)
// - 故障转移链: 主后端失败时依次尝试 fallback 链
// 命中"高可用"考点: 负载分配 + 容错降级 + 自动恢复

#ifndef ROUTER_H
#define ROUTER_H

#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace chat_sdk
{
    class Router
    {
    public:
        Router();

        // 注册一个真实后端模型: model_name + 路由权重 + 故障转移链
        void addBackend(const std::string &model_name, int weight, const std::vector<std::string> &fallback = {});

        // 注册虚拟路由组: route_name -> 后端模型名列表(按各后端自身 weight 分配流量)
        void addRoute(const std::string &route_name, const std::vector<std::string> &backends);

        // 计算一次请求的尝试顺序:
        //   1. 在健康后端中按权重随机选一个主目标
        //   2. 其余健康后端按权重降序排列作为备选
        //   3. 熔断中的后端放到最后兜底
        //   4. 每个候选的 fallback 链追加到队尾(去重)
        std::vector<std::string> plan(const std::string &model_name);

        // 标记故障: 进入熔断窗口,期间不被选中
        void markUnavailable(const std::string &model_name);
        // 手动恢复
        void markAvailable(const std::string &model_name);
        // 是否健康(考虑自动恢复时间)
        bool isHealthy(const std::string &model_name) const;

        // 熔断自动恢复时长,默认 60s
        void setRecoverAfterMs(int ms);
        // 当前路由组名列表(供查询展示)
        std::vector<std::string> routeNames() const;

    private:
        struct BackendInfo
        {
            int weight = 1;
            std::vector<std::string> fallback;
            std::time_t fail_time = 0; // 最近一次故障时间
            int fail_count = 0;        // 连续失败次数
        };

        bool isHealthyLocked(const BackendInfo &info) const;
        // 按权重随机打乱健康候选,返回尝试顺序
        std::vector<std::string> buildOrderLocked(const std::vector<std::string> &backends) const;

        std::map<std::string, BackendInfo> backends_;        // model_name -> 后端信息
        std::map<std::string, std::vector<std::string>> routes_; // route_name -> 后端列表
        mutable std::mutex mutex_;
        int recover_after_ms_ = 60000;
        mutable std::mt19937 rng_;
    };
}

#endif