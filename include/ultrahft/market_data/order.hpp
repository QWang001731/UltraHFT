#pragma once
#include <cstdint>
namespace ultrahft::market_data {

    enum class Side : std::uint8_t
    {
        Buy=0,
        Sell=1
    };

    class Order
    {
        private:
            std::uint64_t order_id{};
            std::uint32_t instrument_id{};
            Side side{};
            std::uint64_t price{};
            std::uint32_t qty{};
            std::uint32_t remaining_qty{};
            Order* next{nullptr};
            Order* prev{nullptr};

        public:
            Order() = default;
            Order(std::uint64_t order_id, std::uint32_t instrument_id, Side side, std::uint64_t price, std::uint32_t qty):
                order_id(order_id), instrument_id(instrument_id), side(side), price(price), qty(qty), remaining_qty(qty)
            {}

            [[nodiscard]] std::uint64_t get_order_id() const noexcept { return order_id; }
            [[nodiscard]] std::uint32_t get_instrument_id() const noexcept { return instrument_id; }
            [[nodiscard]] Side get_side() const noexcept { return side; }
            [[nodiscard]] std::uint64_t get_price() const noexcept { return price; }
            [[nodiscard]] std::uint32_t get_qty() const noexcept { return qty; }
            [[nodiscard]] std::uint32_t get_remaining_qty() const noexcept { return remaining_qty; }

            void set_remaining_qty(std::uint32_t new_qty) noexcept { remaining_qty = new_qty; }

            [[nodiscard]] Order* get_next() const noexcept { return next; }
            [[nodiscard]] Order* get_prev() const noexcept { return prev; }
            void set_next(Order* n) noexcept { next = n; }
            void set_prev(Order* p) noexcept { prev = p; }
    };

}

