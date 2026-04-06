#pragma once
/*
 * FlatOrderMap
 * ────────────
 * Open-addressing hash table that stores Order objects **inline** (no heap
 * allocation per order).  Designed for the hot path in market_data_handler:
 *
 *   insert  O(1) amortised, zero malloc
 *   lookup  O(1) amortised, single cache-line probe in the common case
 *   erase   O(1) amortised (tombstone-free: uses backward-shift deletion)
 *
 * Capacity is fixed at construction (power-of-2).  The table never rehashes
 * in the hot path; size it for the maximum live order count you expect.
 * With default capacity=262144 and ~50k live orders the load stays <20%,
 * so probe chains are essentially always length 1.
 *
 * Layout: one flat array of Slot, each Slot is either empty, occupied, or
 * a "deleted" sentinel.  Backward-shift deletion keeps probe chains short
 * without needing tombstones, so load factors up to ~0.7 stay fast.
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <cassert>
#include "order.hpp"

namespace ultrahft::market_data {

class FlatOrderMap {
public:
    // Maximum live orders. Must be a power of 2. 256 K slots × 64 B = 16 MB.
    // Fits comfortably in L3 for typical instrument order counts.
    static constexpr std::size_t kDefaultCapacity = 1u << 18; // 262144

    explicit FlatOrderMap(std::size_t capacity = kDefaultCapacity)
        : capacity_(next_pow2(capacity))
        , mask_(capacity_ - 1)
        , size_(0)
    {
        slots_ = new Slot[capacity_];
        std::memset(slots_, 0, capacity_ * sizeof(Slot));
    }

    ~FlatOrderMap() { delete[] slots_; }

    FlatOrderMap(const FlatOrderMap&)            = delete;
    FlatOrderMap& operator=(const FlatOrderMap&) = delete;

    FlatOrderMap(FlatOrderMap&& o) noexcept
        : slots_(o.slots_), capacity_(o.capacity_), mask_(o.mask_), size_(o.size_)
    { o.slots_ = nullptr; o.size_ = 0; }

    FlatOrderMap& operator=(FlatOrderMap&& o) noexcept {
        if (this != &o) {
            delete[] slots_;
            slots_    = o.slots_;    o.slots_    = nullptr;
            capacity_ = o.capacity_; o.capacity_ = 0;
            mask_     = o.mask_;     o.mask_     = 0;
            size_     = o.size_;     o.size_     = 0;
        }
        return *this;
    }

    // Insert or replace.  Returns pointer to the stored Order.
    // The table is sized at construction for the expected max live order count.
    // With capacity=262144 and LIVE_ORDER_CAP=50000 the load factor stays ~19%.
    Order* insert(std::uint64_t key, const Order& order) noexcept {
        std::size_t idx = hash(key);
        for (;;) {
            Slot& s = slots_[idx];
            if (!s.occupied) {
                s.key      = key;
                s.order    = order;
                s.occupied = true;
                ++size_;
                return &s.order;
            }
            if (s.key == key) {
                s.order = order;   // update
                return &s.order;
            }
            idx = (idx + 1) & mask_;
        }
    }

    // Returns nullptr if not found.
    Order* find(std::uint64_t key) noexcept {
        std::size_t idx = hash(key);
        for (;;) {
            Slot& s = slots_[idx];
            if (!s.occupied) return nullptr;
            if (s.key == key) return &s.order;
            idx = (idx + 1) & mask_;
        }
    }

    const Order* find(std::uint64_t key) const noexcept {
        std::size_t idx = hash(key);
        for (;;) {
            const Slot& s = slots_[idx];
            if (!s.occupied) return nullptr;
            if (s.key == key) return &s.order;
            idx = (idx + 1) & mask_;
        }
    }

    // Erase by key. Uses backward-shift to avoid tombstones.
    bool erase(std::uint64_t key) noexcept {
        std::size_t idx = hash(key);
        for (;;) {
            Slot& s = slots_[idx];
            if (!s.occupied) return false;
            if (s.key == key) break;
            idx = (idx + 1) & mask_;
        }
        // Backward-shift deletion: pull following entries back into vacated slot.
        slots_[idx].occupied = false;
        --size_;
        std::size_t hole = idx;
        std::size_t cur  = (idx + 1) & mask_;
        for (;;) {
            Slot& s = slots_[cur];
            if (!s.occupied) break;
            std::size_t natural = hash(s.key);
            // Is s displaced from its natural slot?  If so, move it back.
            if (((cur - natural) & mask_) > ((cur - hole) & mask_)) {
                slots_[hole] = s;
                s.occupied   = false;
                hole = cur;
            }
            cur = (cur + 1) & mask_;
        }
        return true;
    }

    [[nodiscard]] std::size_t size()     const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool        empty()    const noexcept { return size_ == 0; }

    void clear() noexcept {
        std::memset(slots_, 0, capacity_ * sizeof(Slot));
        size_ = 0;
    }

private:
    struct Slot {
        std::uint64_t key{0};
        Order         order{};
        bool          occupied{false};
        // pad to next 64-byte cache-line boundary
        char _pad[64 - ((sizeof(std::uint64_t) + sizeof(Order) + sizeof(bool)) % 64 == 0
                        ? 64
                        : (sizeof(std::uint64_t) + sizeof(Order) + sizeof(bool)) % 64)]{};
    };
    static_assert(sizeof(Slot) % 64 == 0, "Slot must be a multiple of one cache line");

    static std::size_t next_pow2(std::size_t n) noexcept {
        if (n == 0) return 1;
        --n;
        n |= n >> 1; n |= n >> 2; n |= n >> 4;
        n |= n >> 8; n |= n >> 16; n |= n >> 32;
        return n + 1;
    }

    [[nodiscard]] std::size_t hash(std::uint64_t key) const noexcept {
        // Fibonacci hashing: multiply by 2^64 / φ, take top bits.
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return static_cast<std::size_t>(key & mask_);
    }

    Slot*       slots_;
    std::size_t capacity_;
    std::size_t mask_;
    std::size_t size_;
};

} // namespace ultrahft::market_data
