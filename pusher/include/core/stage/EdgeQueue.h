#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

namespace reallive::stage {

enum class QueuePolicy {
    kBlock = 0,
    kDropOldest,
    kDropNewest,
    kLatestOnly,
};

struct QueueStats {
    size_t pushed = 0;
    size_t popped = 0;
    size_t dropped = 0;
};

template <typename T>
class EdgeQueue {
public:
    using DropPredicate = std::function<bool(const T&)>;

    explicit EdgeQueue(size_t capacity, QueuePolicy policy = QueuePolicy::kDropOldest)
        : capacity_(capacity > 0 ? capacity : 1), policy_(policy) {}

    void setDropPredicate(DropPredicate pred) {
        std::lock_guard<std::mutex> lock(mutex_);
        dropPredicate_ = std::move(pred);
    }

    bool push(T item, std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) return false;

        const auto waitForSpace = [&]() {
            return closed_ || queue_.size() < capacity_;
        };

        if (policy_ == QueuePolicy::kBlock) {
            if (timeout.count() > 0) {
                if (!cvNotFull_.wait_for(lock, timeout, waitForSpace)) {
                    return false;
                }
            } else {
                cvNotFull_.wait(lock, waitForSpace);
            }
            if (closed_) return false;
        }

        if (queue_.size() >= capacity_) {
            const bool allowEnqueue = applyDropPolicyLocked();
            if (closed_) return false;
            if (!allowEnqueue || queue_.size() >= capacity_) {
                return false;
            }
        }

        queue_.push_back(std::move(item));
        stats_.pushed++;
        cvNotEmpty_.notify_one();
        return true;
    }

    bool pop(T& out, std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto hasItem = [&]() {
            return closed_ || !queue_.empty();
        };

        if (timeout.count() > 0) {
            if (!cvNotEmpty_.wait_for(lock, timeout, hasItem)) {
                return false;
            }
        } else {
            cvNotEmpty_.wait(lock, hasItem);
        }

        if (queue_.empty()) {
            return false;
        }

        out = std::move(queue_.front());
        queue_.pop_front();
        stats_.popped++;
        cvNotFull_.notify_one();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cvNotFull_.notify_all();
        cvNotEmpty_.notify_all();
    }

    bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    size_t capacity() const {
        return capacity_;
    }

    QueueStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

private:
    bool applyDropPolicyLocked() {
        if (queue_.empty()) return true;

        switch (policy_) {
        case QueuePolicy::kDropNewest:
            stats_.dropped++;
            return false;
        case QueuePolicy::kLatestOnly:
            queue_.clear();
            stats_.dropped++;
            return true;
        case QueuePolicy::kDropOldest:
        default:
            dropOneLocked();
            return true;
        }
    }

    void dropOneLocked() {
        if (queue_.empty()) return;

        if (dropPredicate_) {
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                if (dropPredicate_(*it)) {
                    queue_.erase(it);
                    stats_.dropped++;
                    return;
                }
            }
        }

        queue_.pop_front();
        stats_.dropped++;
    }

    const size_t capacity_;
    const QueuePolicy policy_;

    mutable std::mutex mutex_;
    std::condition_variable cvNotEmpty_;
    std::condition_variable cvNotFull_;
    std::deque<T> queue_;
    bool closed_ = false;
    DropPredicate dropPredicate_;
    QueueStats stats_;
};

} // namespace reallive::stage
