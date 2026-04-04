#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include "order.hpp"
#include "price_level.hpp"

namespace ultrahft::market_data {

    class OrderBook {
    private:
        std::uint32_t instrument_id_{};
        std::map<std::uint64_t, PriceLevel, std::greater<std::uint64_t>> bids_;  // descending
        std::map<std::uint64_t, PriceLevel> asks_;  // ascending
        std::map<std::uint64_t, Order*> orders_;  // order_id -> Order*

    public:
        explicit OrderBook(std::uint32_t instrument_id) : instrument_id_(instrument_id) {}

        [[nodiscard]] std::uint32_t get_instrument_id() const noexcept { return instrument_id_; }

        bool add_order(Order* order) noexcept {
            if (!order || orders_.find(order->get_order_id()) != orders_.end()) {
                return false;
            }

            if (order->get_side() == Side::Buy) {
                auto it = bids_.find(order->get_price());
                if (it == bids_.end()) {
                    it = bids_.insert({order->get_price(), PriceLevel(order->get_price())}).first;
                }
                it->second.add_order(order);
            } else {
                auto it = asks_.find(order->get_price());
                if (it == asks_.end()) {
                    it = asks_.insert({order->get_price(), PriceLevel(order->get_price())}).first;
                }
                it->second.add_order(order);
            }
            orders_[order->get_order_id()] = order;
            return true;
        }

        bool cancel_order(std::uint64_t order_id) noexcept {
            auto it = orders_.find(order_id);
            if (it == orders_.end()) {
                return false;
            }

            Order* order = it->second;
            if (order->get_side() == Side::Buy) {
                auto level_it = bids_.find(order->get_price());
                if (level_it != bids_.end()) {
                    level_it->second.remove_order(order);
                    if (level_it->second.is_empty()) {
                        bids_.erase(level_it);
                    }
                }
            } else {
                auto level_it = asks_.find(order->get_price());
                if (level_it != asks_.end()) {
                    level_it->second.remove_order(order);
                    if (level_it->second.is_empty()) {
                        asks_.erase(level_it);
                    }
                }
            }
            orders_.erase(it);
            return true;
        }

        std::uint32_t execute_order(std::uint64_t order_id, std::uint32_t execution_qty) noexcept {
            auto it = orders_.find(order_id);
            if (it == orders_.end()) {
                return 0;
            }

            Order* order = it->second;
            std::uint32_t remaining = order->get_remaining_qty();
            if (remaining == 0) {
                return 0;
            }

            std::uint32_t executed = (execution_qty > remaining) ? remaining : execution_qty;
            order->set_remaining_qty(remaining - executed);

            if (order->get_remaining_qty() == 0) {
                cancel_order(order_id);
            }
            return executed;
        }

        [[nodiscard]] std::optional<std::uint64_t> best_bid() const noexcept {
            if (bids_.empty()) {
                return std::nullopt;
            }
            return bids_.begin()->first;
        }

        [[nodiscard]] std::optional<std::uint64_t> best_ask() const noexcept {
            if (asks_.empty()) {
                return std::nullopt;
            }
            return asks_.begin()->first;
        }

        [[nodiscard]] std::uint32_t get_bid_qty_at(std::uint64_t price) const noexcept {
            auto it = bids_.find(price);
            return (it != bids_.end()) ? it->second.get_total_qty() : 0;
        }

        [[nodiscard]] std::uint32_t get_ask_qty_at(std::uint64_t price) const noexcept {
            auto it = asks_.find(price);
            return (it != asks_.end()) ? it->second.get_total_qty() : 0;
        }
    };

}
