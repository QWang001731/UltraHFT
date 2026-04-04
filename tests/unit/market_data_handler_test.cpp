#include <iostream>
#include <cassert>
#include <atomic>
#include <iomanip>
#include <ultrahft/market_data/market_data_handler.hpp>
#include <ultrahft/market_data/itch_messages.hpp>

using namespace ultrahft::market_data;

// Test statistics
struct TestStats {
    std::atomic<int> add_orders{0};
    std::atomic<int> executions{0};
    std::atomic<int> cancellations{0};
    std::atomic<int> total_events{0};
};

TestStats stats;

// Market data event callback
void market_data_callback(
    MarketDataEventType event_type,
    std::uint32_t,  // instrument_id (unused)
    std::uint64_t,  // timestamp_ns (unused)
    const void*     // data (unused)
) noexcept {
    ++stats.total_events;
    
    switch (event_type) {
        case MarketDataEventType::ORDER_ADDED:
            ++stats.add_orders;
            break;
        case MarketDataEventType::ORDER_EXECUTED:
            ++stats.executions;
            break;
        case MarketDataEventType::ORDER_CANCELLED:
            ++stats.cancellations;
            break;
        default:
            break;
    }
}

// Helper to create ITCH add order message
itch::AddOrderMessage create_add_order(
    std::uint64_t order_ref,
    char side,
    std::uint32_t shares,
    std::uint64_t price
) {
    itch::AddOrderMessage msg{};
    msg.length = sizeof(itch::AddOrderMessage);
    msg.message_type = itch::MSG_ADD_ORDER;
    msg.order_ref = order_ref;
    msg.side = side;
    msg.shares = shares;
    msg.stock = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
    msg.price = price;
    return msg;
}

// Helper to create execute order message
itch::ExecuteOrderMessage create_execute_order(
    std::uint64_t order_ref,
    std::uint32_t executed_shares,
    std::uint64_t price
) {
    itch::ExecuteOrderMessage msg{};
    msg.length = sizeof(itch::ExecuteOrderMessage);
    msg.message_type = itch::MSG_EXECUTE_ORDER;
    msg.order_ref = order_ref;
    msg.executed_shares = executed_shares;
    msg.execution_price = price;
    msg.execution_id = 1;
    return msg;
}

// Helper to create cancel order message
itch::CancelOrderMessage create_cancel_order(
    std::uint64_t order_ref,
    std::uint32_t cancelled_shares
) {
    itch::CancelOrderMessage msg{};
    msg.length = sizeof(itch::CancelOrderMessage);
    msg.message_type = itch::MSG_CANCEL_ORDER;
    msg.order_ref = order_ref;
    msg.cancelled_shares = cancelled_shares;
    return msg;
}

int main() {
    std::cout << "\n=== Market Data Handler Test ===\n\n";
    
    MarketDataHandler handler(1);  // instrument_id = 1
    handler.register_callback(market_data_callback);
    
    // Test 1: Add orders
    std::cout << "Test 1: Add buy and sell orders\n";
    {
        auto add_buy = create_add_order(101, 'B', 100, 10000000);  // Buy 100 @ $100.00
        auto add_sell = create_add_order(102, 'S', 150, 10100000); // Sell 150 @ $101.00
        
        assert(handler.process_message(&add_buy, sizeof(add_buy)));
        assert(handler.process_message(&add_sell, sizeof(add_sell)));
        
        assert(handler.pending_orders() == 2);
        assert(stats.add_orders == 2);
        
        auto best_bid = handler.get_order_book().best_bid();
        assert(best_bid.has_value() && *best_bid == 10000000);
        
        auto best_ask = handler.get_order_book().best_ask();
        assert(best_ask.has_value() && *best_ask == 10100000);
        
        std::cout << "✓ Best bid: $" << (*best_bid) / 1e8 << ", Best ask: $" << (*best_ask) / 1e8 << "\n";
    }
    
    // Test 2: Execute order
    std::cout << "\nTest 2: Execute buy order\n";
    {
        auto execute = create_execute_order(101, 50, 10000000);  // Execute 50 shares @ $100.00
        
        assert(handler.process_message(&execute, sizeof(execute)));
        
        // Order should still be there with 50 shares remaining
        auto qty = handler.get_order_book().get_bid_qty_at(10000000);
        assert(qty == 50);
        assert(stats.executions == 1);
        
        std::cout << "✓ Order 101 partially executed: 50 / 100 shares\n";
    }
    
    // Test 3: Cancel order
    std::cout << "\nTest 3: Cancel remaining shares\n";
    {
        auto cancel = create_cancel_order(101, 50);  // Cancel remaining 50 shares
        
        assert(handler.process_message(&cancel, sizeof(cancel)));
        
        auto qty = handler.get_order_book().get_bid_qty_at(10000000);
        assert(qty == 0);
        assert(stats.cancellations == 1);
        
        std::cout << "✓ Order 101 cancelled: 50 shares\n";
    }
    
    // Test 4: Multiple orders at same price level
    std::cout << "\nTest 4: Multiple orders at same price\n";
    {
        stats.total_events = 0;
        stats.add_orders = 0;
        
        for (int i = 0; i < 5; ++i) {
            auto add = create_add_order(201 + i, 'B', 50, 10000000);
            handler.process_message(&add, sizeof(add));
        }
        
        auto qty = handler.get_order_book().get_bid_qty_at(10000000);
        assert(qty == 50 * 5);
        assert(stats.add_orders == 5);
        
        std::cout << "✓ Added 5 buy orders: total 250 shares @ $100.00\n";
    }
    
    // Test 5: Process large batch
    std::cout << "\nTest 5: Large batch of messages\n";
    {
        handler.reset();
        stats.total_events = 0;
        stats.add_orders = 0;
        stats.executions = 0;
        
        const int BATCH_SIZE = 1000;
        
        // Add orders
        for (int i = 0; i < BATCH_SIZE; ++i) {
            char side = (i % 2 == 0) ? 'B' : 'S';
            std::uint64_t price = 10000000 + (i % 100) * 10000;
            auto add = create_add_order(1000 + i, side, 100, price);
            handler.process_message(&add, sizeof(add));
        }
        
        assert(stats.add_orders == BATCH_SIZE);
        
        // Execute half of them
        for (int i = 0; i < BATCH_SIZE / 2; ++i) {
            auto execute = create_execute_order(
                1000 + i,
                50,
                10000000 + ((1000 + i) % 100) * 10000
            );
            handler.process_message(&execute, sizeof(execute));
        }
        
        assert(stats.executions == BATCH_SIZE / 2);
        assert(handler.total_messages() == BATCH_SIZE + BATCH_SIZE / 2);
        
        std::cout << "✓ Processed " << (BATCH_SIZE + BATCH_SIZE / 2) << " messages\n";
        std::cout << "  - " << BATCH_SIZE << " add orders\n";
        std::cout << "  - " << (BATCH_SIZE / 2) << " executions\n";
    }
    
    // Test 6: Statistics
    std::cout << "\nTest 6: Handler Statistics\n";
    {
        std::cout << std::left
                  << std::setw(30) << "Total messages:"
                  << handler.total_messages() << "\n"
                  << std::setw(30) << "Messages processed:"
                  << handler.messages_processed() << "\n"
                  << std::setw(30) << "Pending orders:"
                  << handler.pending_orders() << "\n"
                  << std::setw(30) << "OrderBook bids:"
                  << handler.get_order_book().best_bid().has_value() << "\n"
                  << std::setw(30) << "OrderBook asks:"
                  << handler.get_order_book().best_ask().has_value() << "\n";
    }
    
    // Test 7: Invalid/rejected messages
    std::cout << "\nTest 7: Error handling\n";
    {
        // Empty message
        assert(!handler.process_message(nullptr, 0));
        
        // Too short message
        uint8_t short_msg[2] = {0};
        assert(!handler.process_message(short_msg, 2));
        
        // Invalid message type
        uint8_t invalid_msg[3] = {0, 0, 'Z'};  // Invalid type 'Z'
        assert(!handler.process_message(invalid_msg, 3));
        
        std::cout << "✓ Error handling works correctly\n";
    }
    
    std::cout << "\n=== All Market Data Handler Tests Passed! ===\n\n";
    
    return 0;
}
