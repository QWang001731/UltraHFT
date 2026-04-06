#pragma once

/*
 * LatencyHistogram
 * ────────────────
 * Lock-free, allocation-free latency histogram for the hot receive path.
 *
 * Design:
 *  • 64 power-of-2 buckets covering 1 ns – ~9.2 × 10^18 ns.
 *  • Bucket index = 63 - __builtin_clzll(sample_ns) for sample_ns > 0,
 *    index 0 for sample_ns == 0.
 *  • All counters are plain uint64_t – intended for single-thread use
 *    (one writer: the RX polling thread).  Call reset() between windows.
 *  • percentile() does a linear scan of 64 buckets → negligible cost when
 *    called once per second from the reporting path.
 *
 * Usage (hot path – no branches, no memory allocations):
 *   uint64_t t0 = rdtsc();
 *   ... do work ...
 *   uint64_t t1 = rdtsc();
 *   hist.record_cycles(t1 - t0, tsc_hz);   // converts to ns internally
 *   // --- OR, if you already have ns ---
 *   hist.record_ns(latency_ns);
 */

#include <array>
#include <cstdint>
#include <cstring>

#if __has_include(<rte_cycles.h>)
#  include <rte_cycles.h>
#  define UHFT_HAS_RDTSC_DPDK 1
#else
#  define UHFT_HAS_RDTSC_DPDK 0
#endif

namespace ultrahft::common {

// ── Raw TSC helper ──────────────────────────────────────────────────────────
inline std::uint64_t rdtsc() noexcept {
#if UHFT_HAS_RDTSC_DPDK
    return rte_get_tsc_cycles();
#else
    std::uint64_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (hi << 32) | lo;
#endif
}

// ── Histogram ───────────────────────────────────────────────────────────────
class LatencyHistogram {
public:
    static constexpr int kBuckets = 64;

    LatencyHistogram() noexcept { reset(); }

    // Record a latency already expressed in nanoseconds.
    void record_ns(std::uint64_t ns) noexcept {
        ++total_;
        sum_ns_ += ns;
        if (ns < min_ns_) min_ns_ = ns;
        if (ns > max_ns_) max_ns_ = ns;
        ++buckets_[bucket_index(ns)];
    }

    // Record a raw TSC delta; tsc_hz = rte_get_tsc_hz().
    void record_cycles(std::uint64_t cycles, std::uint64_t tsc_hz) noexcept {
        if (tsc_hz == 0) return;
        // Multiply first to preserve precision: (cycles * 1e9) / hz
        // Use 128-bit intermediate via __uint128_t to avoid overflow.
        const std::uint64_t ns =
            static_cast<std::uint64_t>(
                (static_cast<unsigned __int128>(cycles) * 1'000'000'000ULL) / tsc_hz);
        record_ns(ns);
    }

    // Compute approximate percentile (0–100).  O(64) – call only on report path.
    [[nodiscard]] std::uint64_t percentile(double pct) const noexcept {
        if (total_ == 0) return 0;
        const std::uint64_t target = static_cast<std::uint64_t>(
            (pct / 100.0) * static_cast<double>(total_));
        std::uint64_t cumulative = 0;
        for (int b = 0; b < kBuckets; ++b) {
            cumulative += buckets_[b];
            if (cumulative > target) {
                // Return the upper bound of bucket b.
                return (b == 0) ? 1ULL : (1ULL << b);
            }
        }
        return max_ns_;
    }

    [[nodiscard]] std::uint64_t min_ns()  const noexcept { return (total_ > 0) ? min_ns_ : 0; }
    [[nodiscard]] std::uint64_t max_ns()  const noexcept { return max_ns_; }
    [[nodiscard]] std::uint64_t mean_ns() const noexcept {
        return (total_ > 0) ? (sum_ns_ / total_) : 0;
    }
    [[nodiscard]] std::uint64_t total()   const noexcept { return total_; }

    void reset() noexcept {
        buckets_.fill(0);
        total_  = 0;
        sum_ns_ = 0;
        min_ns_ = UINT64_MAX;
        max_ns_ = 0;
    }

private:
    static int bucket_index(std::uint64_t ns) noexcept {
        if (ns == 0) return 0;
        // Position of the most-significant set bit → bucket index.
        // __builtin_clzll(ns) counts leading zeros in a 64-bit value.
        int idx = 63 - __builtin_clzll(ns);
        return (idx < kBuckets) ? idx : (kBuckets - 1);
    }

    std::array<std::uint64_t, kBuckets> buckets_{};
    std::uint64_t total_{0};
    std::uint64_t sum_ns_{0};
    std::uint64_t min_ns_{UINT64_MAX};
    std::uint64_t max_ns_{0};
};

} // namespace ultrahft::common
