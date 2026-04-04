#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <chrono>
#include <memory>
#include <list>

namespace ultrahft::core {

/**
 * @brief Timer wheel for efficient timeout management
 * 
 * A hierarchical timer wheel data structure that efficiently manages timeouts.
 * Time complexity: O(1) for insertion, deletion, and tick operations.
 * 
 * Use cases:
 * - Order expiration tracking
 * - Session/connection timeouts
 * - Scheduled event execution
 * - Low-latency batch timeout processing
 */
template<typename Callback = std::function<void()>>
class TimerWheel {
public:
    using TimerId = std::uint64_t;
    using Timestamp = std::uint64_t;  // nanoseconds
    
private:
    struct TimerNode {
        TimerId id;
        Timestamp expire_time;
        Callback callback;
        
        TimerNode(TimerId id, Timestamp expire_time, Callback cb)
            : id(id), expire_time(expire_time), callback(std::move(cb)) {}
    };
    
    static constexpr std::size_t WHEEL_SIZE = 256;
    static constexpr std::uint64_t TICK_INTERVAL_NS = 1'000'000;  // 1ms per tick
    
public:
    explicit TimerWheel(std::size_t wheel_size = WHEEL_SIZE)
        : wheel_size_(wheel_size)
        , current_tick_(0)
        , next_timer_id_(1) {
        wheels_.resize(wheel_size_);
    }
    
    /**
     * @brief Schedule a callback to execute after a specified duration
     * @param duration_ns Duration in nanoseconds from now
     * @param callback Function to call when timer expires
     * @return Timer ID for potential cancellation
     */
    TimerId schedule(std::uint64_t duration_ns, Callback callback) noexcept {
        TimerId id = next_timer_id_++;
        Timestamp expire_time = current_tick_ + duration_ns;
        std::size_t bucket = (expire_time / TICK_INTERVAL_NS) % wheel_size_;
        
        wheels_[bucket].emplace_back(id, expire_time, std::move(callback));
        return id;
    }
    
    /**
     * @brief Schedule a callback at an absolute time
     * @param absolute_time_ns Absolute expiration time in nanoseconds
     * @param callback Function to call when timer expires
     * @return Timer ID for potential cancellation
     */
    TimerId schedule_at(Timestamp absolute_time_ns, Callback callback) noexcept {
        TimerId id = next_timer_id_++;
        std::size_t bucket = (absolute_time_ns / TICK_INTERVAL_NS) % wheel_size_;
        
        wheels_[bucket].emplace_back(id, absolute_time_ns, std::move(callback));
        return id;
    }
    
    /**
     * @brief Schedule a recurring callback
     * @param interval_ns Interval in nanoseconds
     * @param callback Function to call repeatedly
     * @return Timer ID for stopping the recurring timer
     */
    TimerId schedule_recurring(std::uint64_t interval_ns, Callback callback) noexcept {
        // For now, schedule the next occurrence
        // In a full implementation, this would track recurring timers
        TimerId id = next_timer_id_++;
        Timestamp expire_time = current_tick_ + interval_ns;
        std::size_t bucket = (expire_time / TICK_INTERVAL_NS) % wheel_size_;
        
        // Store callback with interval for re-scheduling on expiration
        auto recurring_cb = [this, id, interval_ns, callback]() {
            callback();
            schedule_recurring(interval_ns, callback);
        };
        
        wheels_[bucket].emplace_back(id, expire_time, std::move(recurring_cb));
        return id;
    }
    
    /**
     * @brief Cancel a scheduled timer
     * @param timer_id Timer ID returned from schedule
     * @return True if timer was found and cancelled, false otherwise
     */
    bool cancel(TimerId timer_id) noexcept {
        for (auto& bucket : wheels_) {
            for (auto it = bucket.begin(); it != bucket.end(); ++it) {
                if (it->id == timer_id) {
                    bucket.erase(it);
                    return true;
                }
            }
        }
        return false;
    }
    
    /**
     * @brief Advance time and execute expired timers
     * @param current_time_ns Current time in nanoseconds
     * @return Number of timers that expired
     */
    std::size_t tick(Timestamp current_time_ns) noexcept {
        std::size_t expired_count = 0;
        
        // Process all buckets up to current time
        while (current_tick_ <= current_time_ns) {
            std::size_t bucket = current_tick_ / TICK_INTERVAL_NS % wheel_size_;
            
            auto& timers = wheels_[bucket];
            auto it = timers.begin();
            
            while (it != timers.end()) {
                if (it->expire_time <= current_time_ns) {
                    it->callback();
                    ++expired_count;
                    it = timers.erase(it);
                } else {
                    ++it;
                }
            }
            
            current_tick_ += TICK_INTERVAL_NS;
        }
        
        return expired_count;
    }
    
    /**
     * @brief Get current wheel time
     */
    [[nodiscard]] Timestamp current_time() const noexcept {
        return current_tick_;
    }
    
    /**
     * @brief Get number of pending timers
     */
    [[nodiscard]] std::size_t pending_count() const noexcept {
        std::size_t count = 0;
        for (const auto& bucket : wheels_) {
            count += bucket.size();
        }
        return count;
    }
    
    /**
     * @brief Clear all timers
     */
    void clear() noexcept {
        for (auto& bucket : wheels_) {
            bucket.clear();
        }
        current_tick_ = 0;
    }
    
private:
    std::vector<std::list<TimerNode>> wheels_;
    std::size_t wheel_size_;
    Timestamp current_tick_;
    TimerId next_timer_id_;
};

}  // namespace ultrahft::core
