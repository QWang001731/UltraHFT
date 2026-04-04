#pragma once
#include <cstdint>
#include "order.hpp"

namespace ultrahft::market_data {

    class PriceLevel {
    private:
        std::uint64_t price_{};
        Order* head_{nullptr};
        Order* tail_{nullptr};

    public:
        PriceLevel() = default;
        explicit PriceLevel(std::uint64_t price) : price_(price) {}

        [[nodiscard]] std::uint64_t get_price() const noexcept { return price_; }
        
        /**
         * @brief Get total quantity at this price level
         * Sums remaining quantities of all orders at this price
         */
        [[nodiscard]] std::uint32_t get_total_qty() const noexcept {
            std::uint32_t total = 0;
            for (Order* order = head_; order != nullptr; order = order->get_next()) {
                total += order->get_remaining_qty();
            }
            return total;
        }
        
        [[nodiscard]] Order* get_head() const noexcept { return head_; }
        [[nodiscard]] Order* get_tail() const noexcept { return tail_; }

        void add_order(Order* order) noexcept {
            if (head_ == nullptr) {
                head_ = tail_ = order;
                order->set_next(nullptr);
                order->set_prev(nullptr);
            } else {
                tail_->set_next(order);
                order->set_prev(tail_);
                order->set_next(nullptr);
                tail_ = order;
            }
        }

        void remove_order(Order* order) noexcept {
            if (order->get_prev()) {
                order->get_prev()->set_next(order->get_next());
            } else {
                head_ = order->get_next();
            }
            if (order->get_next()) {
                order->get_next()->set_prev(order->get_prev());
            } else {
                tail_ = order->get_prev();
            }
        }

        bool is_empty() const noexcept { return head_ == nullptr; }
    };

}
