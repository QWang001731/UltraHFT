#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <optional>
#include "itch_messages.hpp"
#include "order.hpp"
#include "order_book.hpp"

namespace ultrahft::market_data {

/**
 * @brief Market data event types
 */
enum class MarketDataEventType {
    ORDER_ADDED,
    ORDER_EXECUTED,
    ORDER_CANCELLED,
    ORDER_DELETED,
    ORDER_REPLACED,
    TRADE,
    SYSTEM_EVENT,
    UNKNOWN
};

/**
 * @brief Market data event callback
 * Parameters: event_type, instrument_id, timestamp_ns, custom_data_ptr
 */
using MarketDataCallback = std::function<void(
    MarketDataEventType, 
    std::uint32_t,
    std::uint64_t,
    const void*
)>;

/**
 * @brief High-performance market data handler
 * 
 * Parses ITCH messages and:
 * 1. Updates OrderBook with new orders and executions
 * 2. Triggers callbacks for market data events
 * 3. Supports DPDK packet processing for zero-copy parsing
 */
class MarketDataHandler {
public:
    explicit MarketDataHandler(std::uint32_t instrument_id)
        : instrument_id_(instrument_id)
        , order_book_(instrument_id)
        , total_messages_(0)
        , messages_processed_(0) {}
    
    /**
     * @brief Register a callback for market data events
     */
    void register_callback(MarketDataCallback callback) noexcept {
        callbacks_.push_back(std::move(callback));
    }
    
    /**
     * @brief Process ITCH add order message
     */
    void process_add_order(const itch::AddOrderMessage* msg) noexcept {
        if (!msg) return;

        Side side = (msg->side == 'B') ? Side::Buy : Side::Sell;
        // Construct Order on the stack; order_book_.add_order copies it into
        // the FlatOrderMap's inline slot — zero heap allocation.
        Order order(msg->order_ref, instrument_id_, side, msg->price, msg->shares);

        if (order_book_.add_order(&order)) {
            fire_callback(MarketDataEventType::ORDER_ADDED, msg->timestamp, msg);
            messages_processed_++;
        }
    }
    
    /**
     * @brief Process ITCH execute order message
     */
    void process_execute_order(const itch::ExecuteOrderMessage* msg) noexcept {
        if (!msg) return;
        
        std::uint32_t executed = order_book_.execute_order(
            msg->order_ref,
            msg->executed_shares
        );
        
        if (executed > 0) {
            fire_callback(
                MarketDataEventType::ORDER_EXECUTED,
                msg->timestamp,
                msg
            );
            messages_processed_++;
        }
    }
    
    /**
     * @brief Process ITCH cancel order message
     */
    void process_cancel_order(const itch::CancelOrderMessage* msg) noexcept {
        if (!msg) return;

        if (order_book_.cancel_order(msg->order_ref)) {
            fire_callback(MarketDataEventType::ORDER_CANCELLED, msg->timestamp, msg);
            messages_processed_++;
        }
    }

    /**
     * @brief Process ITCH delete order message
     */
    void process_delete_order(const itch::DeleteOrderMessage* msg) noexcept {
        if (!msg) return;

        if (order_book_.cancel_order(msg->order_ref)) {
            fire_callback(MarketDataEventType::ORDER_DELETED, msg->timestamp, msg);
            messages_processed_++;
        }
    }

    /**
     * @brief Process ITCH replace order message
     */
    void process_replace_order(const itch::ReplaceOrderMessage* msg) noexcept {
        if (!msg) return;

        order_book_.cancel_order(msg->order_ref);
        fire_callback(MarketDataEventType::ORDER_REPLACED, msg->timestamp, msg);
        messages_processed_++;
    }
    
    /**
     * @brief Process ITCH trade message
     */
    void process_trade(const itch::TradeMessage* msg) noexcept {
        if (!msg) return;
        
        fire_callback(
            MarketDataEventType::TRADE,
            msg->timestamp,
            msg
        );
        messages_processed_++;
    }
    
    /**
     * @brief Parse and process a complete ITCH message
     * @param data Pointer to message data
     * @param size Size of message data
     * @return true if message was successfully processed
     */
    bool process_message(const void* data, std::size_t size) noexcept {
        if (!data || size < 3) return false;
        
        total_messages_++;
        
        const auto* header = static_cast<const itch::MessageHeader*>(data);
        char msg_type = header->message_type;
        
        switch (msg_type) {
            case itch::MSG_ADD_ORDER:
            case itch::MSG_ADD_ORDER_MPID: {
                auto msg = itch::cast_message<itch::AddOrderMessage>(data, size);
                if (msg) process_add_order(msg);
                return msg != nullptr;
            }
            
            case itch::MSG_EXECUTE_ORDER:
            case itch::MSG_EXECUTE_ORDER_WITH_PRICE: {
                auto msg = itch::cast_message<itch::ExecuteOrderMessage>(data, size);
                if (msg) process_execute_order(msg);
                return msg != nullptr;
            }
            
            case itch::MSG_CANCEL_ORDER: {
                auto msg = itch::cast_message<itch::CancelOrderMessage>(data, size);
                if (msg) process_cancel_order(msg);
                return msg != nullptr;
            }
            
            case itch::MSG_DELETE_ORDER: {
                auto msg = itch::cast_message<itch::DeleteOrderMessage>(data, size);
                if (msg) process_delete_order(msg);
                return msg != nullptr;
            }
            
            case itch::MSG_REPLACE_ORDER: {
                auto msg = itch::cast_message<itch::ReplaceOrderMessage>(data, size);
                if (msg) process_replace_order(msg);
                return msg != nullptr;
            }
            
            case itch::MSG_TRADE: {
                auto msg = itch::cast_message<itch::TradeMessage>(data, size);
                if (msg) process_trade(msg);
                return msg != nullptr;
            }
            
            default:
                return false;
        }
    }
    
    /**
     * @brief Get OrderBook reference
     */
    [[nodiscard]] OrderBook& get_order_book() noexcept {
        return order_book_;
    }
    
    [[nodiscard]] const OrderBook& get_order_book() const noexcept {
        return order_book_;
    }
    
    /**
     * @brief Get statistics
     */
    [[nodiscard]] std::uint64_t total_messages() const noexcept {
        return total_messages_;
    }
    
    [[nodiscard]] std::uint64_t messages_processed() const noexcept {
        return messages_processed_;
    }
    
    [[nodiscard]] std::size_t pending_orders() const noexcept {
        return order_book_.order_count();
    }

    /**
     * @brief Clear state
     */
    void reset() noexcept {
        order_book_ = OrderBook(instrument_id_);
        total_messages_ = 0;
        messages_processed_ = 0;
    }
    
private:
    void fire_callback(
        MarketDataEventType event_type,
        std::uint64_t timestamp_ns,
        const void* data
    ) noexcept {
        for (auto& callback : callbacks_) {
            callback(event_type, instrument_id_, timestamp_ns, data);
        }
    }
    
    std::uint32_t instrument_id_;
    OrderBook order_book_;
    std::vector<MarketDataCallback> callbacks_;

    std::uint64_t total_messages_;
    std::uint64_t messages_processed_;
};

}  // namespace ultrahft::market_data
