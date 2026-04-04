#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#if __has_include(<rte_eal.h>)
#define ULTRAHFT_HAS_DPDK 1
#include <rte_byteorder.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#else
#define ULTRAHFT_HAS_DPDK 0
#endif

/*
 * DPDK Packet Handler for Market Data
 * 
 * This module provides high-performance packet processing using DPDK.
 * DPDK (Data Plane Development Kit) offers:
 * - Zero-copy packet access via memory-mapped buffers
 * - Hardware offloading capabilities
 * - Lock-free ring buffers for inter-core communication
 * - Efficient NIC management
 * 
 * When DPDK is not available, this can fall back to standard socket processing.
 */

namespace ultrahft::market_data::dpdk {

/**
 * @brief DPDK-based packet processor configuration
 */
struct DpdkConfig {
    std::uint16_t port_id = 0;              // Physical port ID
    std::uint16_t queue_id = 0;             // RX queue ID
    std::uint32_t mbuf_pool_size = 8191;    // Size of mbuf pool (must be power of 2 - 1)
    std::uint16_t burst_size = 32;          // Packets to process per burst
    std::uint16_t promisc_mode = 1;         // Enable promiscuous mode
    std::uint16_t enable_flow_control = 1;  // Enable flow control
    std::uint16_t receive_queue_count = 1;  // Number of RX queues
    std::uint16_t transmit_queue_count = 1; // Number of TX queues
};

/**
 * @brief Callback for processed packets
 * Parameters: packet_data, packet_length, timestamp_ns, user_context
 */
using PacketProcessCallback = std::function<void(
    const void*,
    std::uint16_t,
    std::uint64_t,
    void*
)>;

/**
 * @brief DPDK Packet Handler
 * 
 * High-performance packet processing with DPDK integration.
 * Handles:
 * - Packet reception from NIC
 * - UDP payload extraction (memcpy-free)
 * - Timestamp capture from hardware
 * - Batch processing for efficiency
 */
class DpdkPacketHandler {
public:
    explicit DpdkPacketHandler(const DpdkConfig& config = DpdkConfig())
        : config_(config)
        , packets_received_(0)
        , packets_processed_(0)
        , packets_dropped_(0)
        , tsc_hz_(0)
    #if ULTRAHFT_HAS_DPDK
        , mbuf_pool_(nullptr)
    #endif
        , initialized_(false) {}
    
    /**
     * @brief Initialize DPDK and NIC
     * 
     * Initializes EAL, mempool, and NIC port/queue configuration.
     */
    bool init(int argc, char* argv[]) noexcept {
#if ULTRAHFT_HAS_DPDK
        if (initialized_) {
            return true;
        }

        const int eal_ret = rte_eal_init(argc, argv);
        if (eal_ret < 0) {
            return false;
        }

        if (rte_eth_dev_count_avail() == 0 || !rte_eth_dev_is_valid_port(config_.port_id)) {
            return false;
        }

        char pool_name[64]{};
        std::snprintf(pool_name, sizeof(pool_name), "UHFT_MBUF_POOL_%u_%u", config_.port_id, config_.queue_id);
        mbuf_pool_ = rte_pktmbuf_pool_create(
            pool_name,
            config_.mbuf_pool_size,
            256,
            0,
            RTE_MBUF_DEFAULT_BUF_SIZE,
            rte_socket_id());
        if (mbuf_pool_ == nullptr) {
            return false;
        }

        rte_eth_conf port_conf{};
        port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;

        if (rte_eth_dev_configure(
                config_.port_id,
                config_.receive_queue_count,
                config_.transmit_queue_count,
                &port_conf) < 0) {
            return false;
        }

        for (std::uint16_t q = 0; q < config_.receive_queue_count; ++q) {
            if (rte_eth_rx_queue_setup(
                    config_.port_id,
                    q,
                    1024,
                    rte_eth_dev_socket_id(config_.port_id),
                    nullptr,
                    mbuf_pool_) < 0) {
                return false;
            }
        }

        for (std::uint16_t q = 0; q < config_.transmit_queue_count; ++q) {
            if (rte_eth_tx_queue_setup(
                    config_.port_id,
                    q,
                    1024,
                    rte_eth_dev_socket_id(config_.port_id),
                    nullptr) < 0) {
                return false;
            }
        }

        if (rte_eth_dev_start(config_.port_id) < 0) {
            return false;
        }

        if (config_.promisc_mode != 0) {
            rte_eth_promiscuous_enable(config_.port_id);
        }

        tsc_hz_ = rte_get_tsc_hz();
#else
        (void)argc;
        (void)argv;
        return false;
#endif
        initialized_ = true;
        return true;
    }
    
    /**
     * @brief Process packets from NIC
     * 
     * Non-blocking burst receive and process.
     * Returns number of packets processed.
     */
    std::uint16_t process_burst(PacketProcessCallback callback, void* ctx = nullptr) noexcept {
#if ULTRAHFT_HAS_DPDK
        if (!initialized_) {
            return 0;
        }

        std::array<rte_mbuf*, 256> pkts{};
        const std::uint16_t burst = (config_.burst_size > pkts.size())
                                        ? static_cast<std::uint16_t>(pkts.size())
                                        : config_.burst_size;

        const std::uint16_t nb_rx = rte_eth_rx_burst(
            config_.port_id,
            config_.queue_id,
            pkts.data(),
            burst);

        packets_received_ += nb_rx;
        if (nb_rx == 0) {
            return 0;
        }

        for (std::uint16_t i = 0; i < nb_rx; ++i) {
            rte_mbuf* mbuf = pkts[i];
            const void* pkt_data = rte_pktmbuf_mtod(mbuf, const void*);
            const std::uint16_t pkt_len = static_cast<std::uint16_t>(rte_pktmbuf_pkt_len(mbuf));
            const std::uint64_t ts_ns = get_timestamp_ns();

            bool processed = false;
            if (callback) {
                callback(pkt_data, pkt_len, ts_ns, ctx);
                processed = true;
            } else {
                for (auto& cb : callbacks_) {
                    cb(pkt_data, pkt_len, ts_ns, ctx);
                    processed = true;
                }
            }

            if (processed) {
                ++packets_processed_;
            } else {
                ++packets_dropped_;
            }

            rte_pktmbuf_free(mbuf);
        }
        return nb_rx;
#else
        (void)callback;
        (void)ctx;
        return 0;
#endif
    }
    
    /**
     * @brief Register a packet callback
     */
    void register_callback(PacketProcessCallback callback) noexcept {
        callbacks_.push_back(std::move(callback));
    }
    
    /**
     * @brief Extract UDP payload from packet (zero-copy in DPDK)
     * 
     * DPDK allows direct access to packet data without memcpy:
     * const uint8_t* pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t*);
     * 
     * Returns pointer to UDP payload and its length.
     */
    static std::optional<std::pair<const void*, std::uint16_t>> 
    extract_udp_payload(const void* packet_data, std::uint16_t packet_len) noexcept {
        if (packet_data == nullptr || packet_len < 14) {
            return std::nullopt;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(packet_data);

        // EtherType is at bytes 12..13
        std::uint16_t ether_type = static_cast<std::uint16_t>((bytes[12] << 8) | bytes[13]);
        std::size_t offset = 14;

        // VLAN tag support
        if (ether_type == 0x8100 || ether_type == 0x88a8) {
            if (packet_len < 18) {
                return std::nullopt;
            }
            ether_type = static_cast<std::uint16_t>((bytes[16] << 8) | bytes[17]);
            offset = 18;
        }

        // IPv4 only
        if (ether_type != 0x0800 || packet_len < offset + 20) {
            return std::nullopt;
        }

        const std::uint8_t version_ihl = bytes[offset];
        const std::uint8_t ihl = static_cast<std::uint8_t>(version_ihl & 0x0F);
        const std::size_t ip_header_len = static_cast<std::size_t>(ihl) * 4U;
        if ((version_ihl >> 4) != 4 || ihl < 5 || packet_len < offset + ip_header_len + 8) {
            return std::nullopt;
        }

        const std::uint8_t protocol = bytes[offset + 9];
        if (protocol != 17) {
            return std::nullopt;
        }

        const std::size_t udp_offset = offset + ip_header_len;
        const std::uint16_t udp_len = static_cast<std::uint16_t>((bytes[udp_offset + 4] << 8) | bytes[udp_offset + 5]);
        if (udp_len < 8) {
            return std::nullopt;
        }

        const std::size_t payload_offset = udp_offset + 8;
        const std::size_t payload_len = static_cast<std::size_t>(udp_len - 8);
        if (packet_len < payload_offset + payload_len) {
            return std::nullopt;
        }

        return std::make_pair(static_cast<const void*>(bytes + payload_offset),
                              static_cast<std::uint16_t>(payload_len));
    }
    
    /**
     * @brief Get high-resolution hardware timestamp
     * 
     * DPDK can provide hardware timestamps from NIC:
     * uint64_t ts = rte_get_tsc_cycles() with proper calibration
     */
    [[nodiscard]] static std::uint64_t get_timestamp_ns() noexcept {
#if ULTRAHFT_HAS_DPDK
        const std::uint64_t hz = rte_get_tsc_hz();
        if (hz == 0) {
            return 0;
        }
        const std::uint64_t cycles = rte_get_tsc_cycles();
        return (cycles * 1000000000ULL) / hz;
#else
        return 0;
#endif
    }
    
    /**
     * @brief Shutdown and cleanup DPDK resources
     */
    void shutdown() noexcept {
        if (!initialized_) return;

#if ULTRAHFT_HAS_DPDK
        rte_eth_dev_stop(config_.port_id);
        rte_eth_dev_close(config_.port_id);
        if (mbuf_pool_ != nullptr) {
            rte_mempool_free(mbuf_pool_);
            mbuf_pool_ = nullptr;
        }
#endif

        initialized_ = false;
    }
    
    /**
     * @brief Get statistics
     */
    [[nodiscard]] std::uint64_t packets_received() const noexcept { return packets_received_; }
    [[nodiscard]] std::uint64_t packets_processed() const noexcept { return packets_processed_; }
    [[nodiscard]] std::uint64_t packets_dropped() const noexcept { return packets_dropped_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }
    
    virtual ~DpdkPacketHandler() {
        shutdown();
    }
    
private:
    DpdkConfig config_;
    std::vector<PacketProcessCallback> callbacks_;
    std::uint64_t packets_received_;
    std::uint64_t packets_processed_;
    std::uint64_t packets_dropped_;
    std::uint64_t tsc_hz_;
#if ULTRAHFT_HAS_DPDK
    rte_mempool* mbuf_pool_;
#endif
    bool initialized_;
};

}  // namespace ultrahft::market_data::dpdk
