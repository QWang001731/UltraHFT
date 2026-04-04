#include <cassert>
#include <iostream>
#include <ultrahft/market_data/order_book.hpp>

using namespace ultrahft::market_data;

int main() {
    OrderBook book(1);

    // Create and add orders
    Order buy1(101, 1, Side::Buy, 100, 10);
    Order buy2(102, 1, Side::Buy, 99, 5);
    Order sell1(201, 1, Side::Sell, 101, 15);
    Order sell2(202, 1, Side::Sell, 102, 8);

    assert(book.add_order(&buy1));
    assert(book.add_order(&buy2));
    assert(book.add_order(&sell1));
    assert(book.add_order(&sell2));

    // Check best bid/ask
    auto best_bid = book.best_bid();
    assert(best_bid.has_value() && *best_bid == 100);
    
    auto best_ask = book.best_ask();
    assert(best_ask.has_value() && *best_ask == 101);

    // Check quantities at price levels
    assert(book.get_bid_qty_at(100) == 10);
    assert(book.get_bid_qty_at(99) == 5);
    assert(book.get_ask_qty_at(101) == 15);

    // Execute an order
    std::uint32_t executed = book.execute_order(101, 5);
    assert(executed == 5);
    assert(buy1.get_remaining_qty() == 5);

    // Cancel an order
    assert(book.cancel_order(102));
    auto bid_qty_99 = book.get_bid_qty_at(99);
    assert(bid_qty_99 == 0);

    std::cout << "All OrderBook tests passed!\n";
    return 0;
}
