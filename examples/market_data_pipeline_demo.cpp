#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <ultrahft/market_data/dpdk_packet_handler.hpp>
#include <ultrahft/market_data/market_data_handler.hpp>

using ultrahft::market_data::MarketDataEventType;
using ultrahft::market_data::MarketDataHandler;
using ultrahft::market_data::dpdk::DpdkConfig;
using ultrahft::market_data::dpdk::DpdkPacketHandler;

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) {
	g_running.store(false);
}

std::uint16_t read_be16(const std::uint8_t* p) {
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

void process_itch_payload(MarketDataHandler& handler, const std::uint8_t* payload, std::size_t len) {
	// Support MoldUDP64 framing: 10-byte session + 8-byte seq + 2-byte count, then blocks [len|msg]
	if (len >= 20) {
		const std::uint16_t msg_count = read_be16(payload + 18);
		std::size_t off = 20;
		bool parsed_any = false;

		for (std::uint16_t i = 0; i < msg_count; ++i) {
			if (off + 2 > len) {
				break;
			}
			const std::uint16_t block_len = read_be16(payload + off);
			off += 2;
			if (off + block_len > len || block_len == 0) {
				break;
			}

			handler.process_message(payload + off, block_len);
			parsed_any = true;
			off += block_len;
		}

		if (parsed_any) {
			return;
		}
	}

	// Fallback: assume payload itself is one ITCH message.
	handler.process_message(payload, len);
}

}  // namespace

int main(int argc, char* argv[]) {
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);

	DpdkConfig config;
	config.port_id = 0;
	config.queue_id = 0;
	config.burst_size = 64;

	DpdkPacketHandler packet_handler(config);
	if (!packet_handler.init(argc, argv)) {
		std::cerr << "DPDK init failed. Check hugepages, NIC binding, and EAL args.\n";
		return 1;
	}

	MarketDataHandler md_handler(1);
	md_handler.register_callback([](MarketDataEventType ev, std::uint32_t, std::uint64_t, const void*) {
		static std::uint64_t event_count = 0;
		++event_count;
		if (event_count % 100000 == 0) {
			std::cout << "Processed events: " << event_count << " last_type=" << static_cast<int>(ev) << "\n";
		}
	});

	std::cout << "DPDK receiver running. Press Ctrl+C to stop.\n";

	while (g_running.load()) {
		packet_handler.process_burst(
			[&md_handler](const void* packet_data, std::uint16_t packet_len, std::uint64_t, void*) {
				auto udp_payload = DpdkPacketHandler::extract_udp_payload(packet_data, packet_len);
				if (!udp_payload.has_value()) {
					return;
				}

				const auto* payload = static_cast<const std::uint8_t*>(udp_payload->first);
				const std::size_t payload_len = udp_payload->second;
				process_itch_payload(md_handler, payload, payload_len);
			});
	}

	packet_handler.shutdown();

	std::cout << "Stopped. packets_received=" << packet_handler.packets_received()
			  << " packets_processed=" << packet_handler.packets_processed()
			  << " md_messages_total=" << md_handler.total_messages()
			  << " md_messages_parsed=" << md_handler.messages_processed() << "\n";

	return 0;
}
