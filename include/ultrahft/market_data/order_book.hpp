#pragma once
#include <cstdint>
#include <algorithm>
#include <optional>
#include <vector>
#include "order.hpp"
#include "price_level.hpp"
#include "flat_order_map.hpp"

namespace ultrahft::market_data {

    /*
     * OrderBook — cache-friendly rewrite
     * ───────────────────────────────────
     * Old:  std::map (red-black tree) for bids/asks/orders → pointer-chasing,
     *       O(log n) per op, heap allocation per node.
     *
     * New:
     *  • orders_    → FlatOrderMap: open-addressing hash table storing Order
     *                 objects inline — zero malloc per order, O(1) lookup.
     *  • bids/asks  → std::vector<PriceLevel> kept sorted, binary-searched.
     *                 Typical book depth is <200 levels; a sorted vector beats
     *                 a tree on cache performance at this size.
     */
    class OrderBook {
    private:
        std::uint32_t instrument_id_{};

        // Price-level vectors (sorted: bids descending, asks ascending).
        std::vector<PriceLevel> bids_;
        std::vector<PriceLevel> asks_;

        // Order index: order_id → Order stored inline (no heap alloc per order).
        FlatOrderMap orders_;

        // Find or insert a PriceLevel in a sorted vector.
        template<typename Cmp>
        static PriceLevel& find_or_insert(std::vector<PriceLevel>& levels,
                                          std::uint64_t price, Cmp cmp) noexcept {
            auto it = std::lower_bound(levels.begin(), levels.end(), price,
                [&](const PriceLevel& lv, std::uint64_t p) { return cmp(lv.get_price(), p); });
            if (it != levels.end() && it->get_price() == price) return *it;
            return *levels.insert(it, PriceLevel(price));
        }

    public:
        explicit OrderBook(std::uint32_t instrument_id)
            : instrument_id_(instrument_id)
        {
            bids_.reserve(256);
            asks_.reserve(256);
        }

        // Move-constructible/assignable so MarketDataHandler::reset() can work.
        OrderBook(OrderBook&&)            = default;
        OrderBook& operator=(OrderBook&&) = default;
        OrderBook(const OrderBook&)       = delete;
        OrderBook& operator=(const OrderBook&) = delete;

        [[nodiscard]] std::uint32_t get_instrument_id() const noexcept { return instrument_id_; }

        bool add_order(Order* order) noexcept {
            if (!order) return false;
            if (orders_.find(order->get_order_id())) return false; // duplicate

            // Copy order into the flat map's inline slot; get back the stored pointer.
            Order* stored = orders_.insert(order->get_order_id(), *order);

            if (order->get_side() == Side::Buy) {
                auto& lv = find_or_insert(bids_, order->get_price(),
                    [](std::uint64_t a, std::uint64_t b){ return a > b; }); // descending
                lv.add_order(stored);
            } else {
                auto& lv = find_or_insert(asks_, order->get_price(),
                    [](std::uint64_t a, std::uint64_t b){ return a < b; }); // ascending
                lv.add_order(stored);
            }
            return true;
        }

        bool cancel_order(std::uint64_t order_id) noexcept {
            Order* order = orders_.find(order_id);
            if (!order) return false;

            const std::uint64_t price = order->get_price();
            const Side          side  = order->get_side();

            auto& levels = (side == Side::Buy) ? bids_ : asks_;
            auto it = std::find_if(levels.begin(), levels.end(),
                [price](const PriceLevel& lv){ return lv.get_price() == price; });
            if (it != levels.end()) {
                it->remove_order(order);
                if (it->is_empty()) levels.erase(it);
            }
            orders_.erase(order_id);
            return true;
        }

        std::uint32_t execute_order(std::uint64_t order_id,
                                    std::uint32_t execution_qty) noexcept {
            Order* order = orders_.find(order_id);
            if (!order) return 0;

            std::uint32_t remaining = order->get_remaining_qty();
            if (remaining == 0) return 0;

            std::uint32_t executed = (execution_qty > remaining) ? remaining : execution_qty;
            order->set_remaining_qty(remaining - executed);

            if (order->get_remaining_qty() == 0) cancel_order(order_id);
            return executed;
        }

        [[nodiscard]] std::optional<std::uint64_t> best_bid() const noexcept {
            if (bids_.empty()) return std::nullopt;
            return bids_.front().get_price();
        }

        [[nodiscard]] std::optional<std::uint64_t> best_ask() const noexcept {
            if (asks_.empty()) return std::nullopt;
            return asks_.front().get_price();
        }

        [[nodiscard]] std::uint32_t get_bid_qty_at(std::uint64_t price) const noexcept {
            auto it = std::find_if(bids_.begin(), bids_.end(),
                [price](const PriceLevel& lv){ return lv.get_price() == price; });
            return (it != bids_.end()) ? it->get_total_qty() : 0;
        }

        [[nodiscard]] std::uint32_t get_ask_qty_at(std::uint64_t price) const noexcept {
            auto it = std::find_if(asks_.begin(), asks_.end(),
                [price](const PriceLevel& lv){ return lv.get_price() == price; });
            return (it != asks_.end()) ? it->get_total_qty() : 0;
        }

        [[nodiscard]] std::size_t order_count() const noexcept { return orders_.size(); }
    };

} // namespace ultrahft::market_data
