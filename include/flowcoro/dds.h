#pragma once

/**
 * @file dds.h
 * @brief FlowCoro DDS 子集 — 用 Channel 实现 DDS 风格的发布-订阅
 *
 * 设计目标：接口与 OMG DDS 标准对齐，底层用 FlowCoro Channel 驱动，
 * 无需安装任何真实 DDS 中间件。
 *
 * 核心概念映射：
 *   DDS Topic           → flowcoro::dds::Topic<T>
 *   DDS Publisher       → flowcoro::dds::Publisher<T>
 *   DDS DataWriter      → Publisher::write()
 *   DDS Subscriber      → flowcoro::dds::Subscriber<T>
 *   DDS DataReader      → Subscriber::take()
 *   DDS DomainParticipant → flowcoro::dds::DomainParticipant
 *   DDS QoS (HistoryDepth/Deadline) → flowcoro::dds::QoS
 *
 * 使用示例（智能驾驶场景）：
 * @code
 *   using namespace flowcoro::dds;
 *
 *   DomainParticipant dp(0);
 *   auto lidar_topic = dp.create_topic<LidarScan>("sensors/lidar");
 *
 *   auto pub = dp.create_publisher(lidar_topic);
 *   auto sub = dp.create_subscriber(lidar_topic, QoS{.history_depth = 5});
 *
 *   // 发布端（传感器节点）
 *   co_await pub.write(scan);
 *
 *   // 订阅端（感知节点）
 *   auto scan_opt = co_await sub.take();   // std::optional<LidarScan>
 *   if (scan_opt) process(*scan_opt);
 * @endcode
 */

#include <flowcoro/channel.h>
#include <flowcoro/core.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace flowcoro::dds {

// ─────────────────────────────────────────────────────────────────────────────
// QoS — Quality of Service
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 服务质量参数，对齐 DDS HISTORY / DEADLINE QoS 策略。
 *
 *  history_depth : Channel 缓冲容量（对应 DDS HISTORY.depth）
 *  deadline      : 数据读取截止时间（0 = 不启用截止检测）
 */
struct QoS {
    size_t                    history_depth = 10;
    std::chrono::milliseconds deadline{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Topic<T> — 命名数据通道，支持一对多扇出
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 命名 Topic，内部维护所有订阅者的 Channel 引用列表。
 *
 * publish() 将数据广播到所有活跃订阅者。已关闭（expired）的订阅者
 * 会在下次发布时自动清理。
 */
template<typename T>
class Topic {
    std::string                              name_;
    std::mutex                               mu_;
    std::vector<std::weak_ptr<Channel<T>>>   subscribers_;

public:
    explicit Topic(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    /** 内部接口：为新订阅者分配专属 Channel */
    std::shared_ptr<Channel<T>> make_subscriber_channel(size_t depth) {
        auto ch = make_channel<T>(depth);
        std::lock_guard lock(mu_);
        subscribers_.push_back(ch);
        return ch;
    }

    /**
     * 将数据广播给所有活跃订阅者（顺序发送，可靠投递）。
     * 若某订阅者的 Channel 已满，该 Channel 的 send() 会协程挂起等待。
     */
    Task<void> publish(const T& data) {
        // 收集当前活跃订阅者，同时清理失效弱引用
        std::vector<std::shared_ptr<Channel<T>>> active;
        {
            std::lock_guard lock(mu_);
            subscribers_.erase(
                std::remove_if(subscribers_.begin(), subscribers_.end(),
                    [](const std::weak_ptr<Channel<T>>& wp) { return wp.expired(); }),
                subscribers_.end());
            active.reserve(subscribers_.size());
            for (auto& wp : subscribers_) {
                if (auto sp = wp.lock()) active.push_back(sp);
            }
        }

        for (auto& ch : active) {
            if (!ch->is_closed()) {
                co_await ch->send(data);
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Publisher<T> — 薄封装，对应 DDS DataWriter
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
class Publisher {
    std::shared_ptr<Topic<T>> topic_;

public:
    explicit Publisher(std::shared_ptr<Topic<T>> topic) : topic_(std::move(topic)) {}

    /** DDS DataWriter::write() 语义 */
    Task<void> write(const T& data) { co_return co_await topic_->publish(data); }

    /** 别名，与 DDS publish() 命名一致 */
    Task<void> publish(const T& data) { co_return co_await write(data); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Subscriber<T> — 薄封装，对应 DDS DataReader
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
class Subscriber {
    std::shared_ptr<Channel<T>> channel_;
    QoS                         qos_;

public:
    Subscriber(std::shared_ptr<Channel<T>> ch, QoS qos)
        : channel_(std::move(ch)), qos_(std::move(qos)) {}

    /**
     * DDS DataReader::take() 语义。
     * 挂起协程直到数据到达，返回 std::optional<T>。
     * 当 Topic 关闭（close() 被调用）时返回 std::nullopt，
     * 调用方可据此退出循环。
     */
    Task<std::optional<T>> take() {
        co_return co_await channel_->recv();
    }

    /** 关闭本订阅者的接收端（唤醒正在等待的 take()） */
    void close() { channel_->close(); }

    const QoS& qos() const { return qos_; }
};

// ─────────────────────────────────────────────────────────────────────────────
// DomainParticipant — 工厂类，管理 Topic 生命周期
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief DDS 域参与者，统一管理本域内的所有 Topic。
 *
 * 相同名称的 Topic 只创建一次，多次 create_topic() 返回同一实例。
 */
class DomainParticipant {
    int                                        domain_id_;
    std::mutex                                 mu_;
    std::unordered_map<std::string,
                       std::shared_ptr<void>>  topics_;   // type-erased

public:
    explicit DomainParticipant(int domain_id = 0) : domain_id_(domain_id) {}

    int domain_id() const { return domain_id_; }

    /**
     * 创建或获取具名 Topic。
     * 同一名称多次调用返回同一 Topic 实例。
     */
    template<typename T>
    std::shared_ptr<Topic<T>> create_topic(const std::string& name) {
        std::lock_guard lock(mu_);
        auto it = topics_.find(name);
        if (it != topics_.end())
            return std::static_pointer_cast<Topic<T>>(it->second);
        auto topic = std::make_shared<Topic<T>>(name);
        topics_[name] = topic;
        return topic;
    }

    /** 创建 Publisher（DataWriter）*/
    template<typename T>
    Publisher<T> create_publisher(std::shared_ptr<Topic<T>> topic) {
        return Publisher<T>(std::move(topic));
    }

    /**
     * 创建 Subscriber（DataReader）。
     * 每个 Subscriber 拥有独立的缓冲 Channel，互不影响。
     */
    template<typename T>
    Subscriber<T> create_subscriber(std::shared_ptr<Topic<T>> topic,
                                    QoS qos = {}) {
        auto ch = topic->make_subscriber_channel(qos.history_depth);
        return Subscriber<T>(std::move(ch), std::move(qos));
    }
};

} // namespace flowcoro::dds
