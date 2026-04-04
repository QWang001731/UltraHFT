#pragma once

#include <cstdint>
#include <cstring>
#include <array>

/*
 * Nasdaq ITCH 5.0 Message Definitions
 * Used for parsing binary market data from exchange
 */

namespace ultrahft::market_data::itch {

// Message type constants
static constexpr char MSG_SYSTEM_EVENT = 'S';
static constexpr char MSG_STOCK_DIRECTORY = 'R';
static constexpr char MSG_TRADING_STATUS = 'H';
static constexpr char MSG_REG_SHO_SHORT = 'Y';
static constexpr char MSG_IPO_QUOTING = 'K';
static constexpr char MSG_LULD_AUCTION = 'J';
static constexpr char MSG_OPERATIONAL_HALT = 'h';
static constexpr char MSG_ADD_ORDER = 'A';
static constexpr char MSG_ADD_ORDER_MPID = 'F';
static constexpr char MSG_EXECUTE_ORDER = 'E';
static constexpr char MSG_EXECUTE_ORDER_WITH_PRICE = 'C';
static constexpr char MSG_CANCEL_ORDER = 'X';
static constexpr char MSG_DELETE_ORDER = 'D';
static constexpr char MSG_REPLACE_ORDER = 'U';
static constexpr char MSG_TRADE = 'P';
static constexpr char MSG_CROSS_TRADE = 'Q';
static constexpr char MSG_BROKEN_TRADE = 'B';
static constexpr char MSG_NOII = 'I';
static constexpr char MSG_RPII = 'N';
static constexpr char MSG_AUCTION_UPDATE = 'J';

#pragma pack(push, 1)

// Base header present in all ITCH messages
struct MessageHeader {
    uint16_t length;
    char message_type;
};

// System Event Message (S)
struct SystemEventMessage {
    uint16_t length;
    char message_type;  // 'S'
    uint8_t reserved;
    uint32_t timestamp;  // nanoseconds since midnight
    char event_code;     // 'O'=Open, 'S'=Start, 'E'=End, 'C'=Close, etc.
};

// Add Order Message (A)
struct AddOrderMessage {
    uint16_t length;
    char message_type;  // 'A'
    uint8_t reserved;
    uint32_t timestamp;  // nanoseconds since midnight
    uint64_t order_ref;  // Order reference number
    char side;           // 'B'=Buy, 'S'=Sell
    uint32_t shares;     // Number of shares
    std::array<char, 8> stock;  // Stock symbol, left-justified, space-padded
    uint64_t price;      // Price in cents (4 decimal places)
    uint32_t mpid;       // Market Participant ID
};

// Execute Order Message (E)
struct ExecuteOrderMessage {
    uint16_t length;
    char message_type;  // 'E'
    uint8_t reserved;
    uint32_t timestamp;  // nanoseconds since midnight
    uint64_t order_ref;  // Order reference number
    uint32_t executed_shares;  // Number of shares executed
    uint64_t execution_price;  // Execution price in cents
    uint32_t execution_id;     // Unique execution ID
    uint32_t cross_trade_id;   // Cross trade ID (0 if not cross)
};

// Cancel Order Message (X)
struct CancelOrderMessage {
    uint16_t length;
    char message_type;  // 'X'
    uint8_t reserved;
    uint32_t timestamp;  // nanoseconds since midnight
    uint64_t order_ref;  // Order reference number
    uint32_t cancelled_shares;  // Number of shares cancelled
};

// Delete Order Message (D)
struct DeleteOrderMessage {
    uint16_t length;
    char message_type;  // 'D'
    uint8_t reserved;
    uint32_t timestamp;  // nanoseconds since midnight
    uint64_t order_ref;  // Order reference number
};

// Replace Order Message (U)
struct ReplaceOrderMessage {
    uint16_t length;
    char message_type;  // 'U'
    uint8_t reserved;
    uint32_t timestamp;  // nanoseconds since midnight
    uint64_t order_ref;  // Original order reference number
    uint64_t new_order_ref;  // New order reference number
    uint32_t new_shares;  // New number of shares
    uint64_t new_price;   // New price in cents
};

// Trade Message (P)
struct TradeMessage {
    uint16_t length;
    char message_type;  // 'P'
    uint8_t reserved;
    uint32_t timestamp;  // nanoseconds since midnight
    uint64_t order_ref;  // Order reference number (buy side)
    char side;           // 'B'=Buy, 'S'=Sell
    uint32_t shares;     // Number of shares executed
    std::array<char, 8> stock;  // Stock symbol
    uint64_t price;      // Execution price in cents
    uint32_t trade_id;   // Trade ID
    char cross_trade_id; // Cross trade ID type
};

#pragma pack(pop)

/**
 * @brief Helper to safely cast bytes to ITCH message types
 */
template<typename T>
inline const T* cast_message(const void* data, std::size_t available_size) noexcept {
    if (available_size < sizeof(T)) {
        return nullptr;
    }
    return static_cast<const T*>(data);
}

/**
 * @brief Extract stock symbol from ITCH format (left-justified, space-padded)
 */
inline std::string extract_stock_symbol(const std::array<char, 8>& stock_array) noexcept {
    std::string symbol;
    for (char c : stock_array) {
        if (c != ' ') {
            symbol += c;
        }
    }
    return symbol;
}

/**
 * @brief Convert ITCH price (4 decimal places) to uint64_t cents
 * ITCH price format: PPPPPPPP (8 bytes, 4 decimal places)
 * E.g., 0x0000000000989680 = 10000000 cents = $100000.00
 */
inline uint64_t itch_price_to_cents(uint64_t itch_price) noexcept {
    return itch_price;
}

}  // namespace ultrahft::market_data::itch
