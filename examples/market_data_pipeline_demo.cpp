#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <chrono>

#include <ultrahft/market_data/dpdk_packet_handler.hpp>
#include <ultrahft/market_data/market_data_handler.hpp>
#include <ultrahft/common/latency_histogram.hpp>

using ultrahft::market_data::MarketDataEventType;
using ultrahft::market_data::MarketDataHandler;
using ultrahft::market_data::dpdk::DpdkConfig;
using ultrahft::market_data::dpdk::DpdkPacketHandler;

using ultrahft::common::LatencyHistogram;
using ultrahft::common::rdtsc;

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

std::uint16_t read_be16(const std::uint8_t* p) {
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

// Extract the UDP destination port directly from the raw packet (before UDP header strip).
std::uint16_t udp_dst_port(const void* packet_data, std::uint16_t packet_len) {
	const auto* b = static_cast<const std::uint8_t*>(packet_data);
	if (packet_len < 14 + 20 + 4) return 0;
	std::size_t off = 14; // skip Ethernet
	if (packet_len >= 18) {
		const std::uint16_t et = static_cast<std::uint16_t>((b[12] << 8) | b[13]);
		if (et == 0x8100 || et == 0x88a8) off = 18; // VLAN
	}
	const std::uint8_t ihl = b[off] & 0x0F;
	const std::size_t udp_off = off + static_cast<std::size_t>(ihl) * 4;
	if (packet_len < udp_off + 4) return 0;
	return read_be16(b + udp_off + 2); // UDP dst port
}

// Parse MoldUDP64 framing and dispatch each ITCH message.
void process_itch_payload(MarketDataHandler& handler,
                          const std::uint8_t* payload, std::size_t len) {
	// MoldUDP64: 10-byte session + 8-byte seq + 2-byte count, then blocks [2-byte len | msg]
	if (len >= 20) {
		const std::uint16_t msg_count = read_be16(payload + 18);
		std::size_t off = 20;
		bool parsed_any = false;
		for (std::uint16_t i = 0; i < msg_count; ++i) {
			if (off + 2 > len) break;
			const std::uint16_t block_len = read_be16(payload + off);
			off += 2;
			if (block_len == 0 || off + block_len > len) break;
			handler.process_message(payload + off, block_len);
			parsed_any = true;
			off += block_len;
		}
		if (parsed_any) return;
	}
	// Fallback: raw ITCH message
	handler.process_message(payload, len);
}

void print_usage(const char* prog) {
	std::cerr << "Usage: " << prog << " [EAL options] -- [--port <udp_port>]\n"
	          << "  --port   UDP destination port to filter (default: 12300)\n";
}

} // namespace

int main(int argc, char* argv[]) {
	std::signal(SIGINT,  on_signal);
	std::signal(SIGTERM, on_signal);

	// Parse our own args after "--"
	std::uint16_t filter_port = 12300;
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == "--port" && i + 1 < argc) {
			filter_port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
		} else if (std::string(argv[i]) == "--help") {
			print_usage(argv[0]);
			return 0;
		}
	}

	DpdkConfig config;
	config.port_id  = 0;
	config.queue_id = 0;
	config.burst_size = 64;

	DpdkPacketHandler packet_handler(config);
	if (!packet_handler.init(argc, argv)) {
		std::cerr << "DPDK init failed. Check hugepages, NIC binding, and EAL args.\n";
		return 1;
	}

	MarketDataHandler md_handler(1);
	md_handler.register_callback(
		[](MarketDataEventType ev, std::uint32_t, std::uint64_t, const void*) {
			static std::uint64_t event_count = 0;
			if (++event_count % 100'000 == 0) {
				std::cout << "[md] events=" << event_count
				          << " last_type=" << static_cast<int>(ev) << "\n";
			}
		});

	std::cout << "══════════════════════════════════════════════\n"
	          << "  UltraHFT DPDK Market Data Receiver\n"
	          << "  NIC port  : " << config.port_id << " (eno1 / 0000:06:00.0)\n"
	          << "  UDP filter: port " << filter_port << "\n"
	          << "  Send ITCH stream from sender PC:\n"
	          << "    python3 tools/itch_sender.py \\\n"
	          << "      --target <THIS_SERVER_IP> \\\n"
	          << "      --port " << filter_port << " \\\n"
	          << "      --rate 100000 --batch 10\n"
	          << "  Press Ctrl+C to stop.\n"
	          << "══════════════════════════════════════════════\n";

	using clock      = std::chrono::steady_clock;
	using duration_s = std::chrono::duration<double>;

	auto     last_report   = clock::now();
	std::uint64_t last_parsed  = 0;
	std::uint64_t last_total   = 0;
	std::uint64_t last_pkts    = 0;

	// TSC frequency for cycle→ns conversion (constant after boot).
	const std::uint64_t tsc_hz = rte_get_tsc_hz();

	// Per-second histogram (reset each interval) + cumulative (never reset).
	LatencyHistogram lat_window;
	LatencyHistogram lat_total;

	while (g_running.load()) {
		packet_handler.process_burst(
			[&](const void* pkt, std::uint16_t pkt_len, std::uint64_t, void*) {
				// ── t0: first cycle after the mbuf pointer is available ──────
				const std::uint64_t t0 = rdtsc();

				// Port filter — only process packets destined for our ITCH port
				if (filter_port != 0 && udp_dst_port(pkt, pkt_len) != filter_port) {
					return;
				}
				auto udp = DpdkPacketHandler::extract_udp_payload(pkt, pkt_len);
				if (!udp.has_value()) return;
				const auto* payload = static_cast<const std::uint8_t*>(udp->first);
				process_itch_payload(md_handler, payload, udp->second);

				// ── t1: after all ITCH messages in this packet are dispatched ─
				const std::uint64_t t1 = rdtsc();
				const std::uint64_t cycles = t1 - t0;
				lat_window.record_cycles(cycles, tsc_hz);
				lat_total.record_cycles(cycles, tsc_hz);
			});

		// Print throughput + latency stats once per second
		auto now     = clock::now();
		double secs  = std::chrono::duration_cast<duration_s>(now - last_report).count();
		if (secs >= 1.0) {
			std::uint64_t cur_parsed = md_handler.messages_processed();
			std::uint64_t cur_total  = md_handler.total_messages();
			std::uint64_t cur_pkts   = packet_handler.packets_received();

			std::uint64_t d_parsed = cur_parsed - last_parsed;
			std::uint64_t d_total  = cur_total  - last_total;
			std::uint64_t d_pkts   = cur_pkts   - last_pkts;

			double parse_rate = static_cast<double>(d_parsed) / secs;
			double recv_rate  = static_cast<double>(d_total)  / secs;
			double pkt_rate   = static_cast<double>(d_pkts)   / secs;
			double parse_pct  = (d_total > 0)
			                    ? 100.0 * static_cast<double>(d_parsed) / static_cast<double>(d_total)
			                    : 0.0;

			std::cout << "[throughput]"
			          << "  pkts="    << static_cast<std::uint64_t>(pkt_rate)   << "/s"
			          << "  msgs_rx=" << static_cast<std::uint64_t>(recv_rate)  << "/s"
			          << "  parsed="  << static_cast<std::uint64_t>(parse_rate) << "/s"
			          << "  hit%="    << static_cast<int>(parse_pct)            << "%"
			          << "  total_parsed=" << cur_parsed
			          << "\n";

			// Latency percentiles for this 1-second window
			if (lat_window.total() > 0) {
				std::cout << "[latency ns] "
				          << " n="     << lat_window.total()
				          << "  min="  << lat_window.min_ns()
				          << "  mean=" << lat_window.mean_ns()
				          << "  p50="  << lat_window.percentile(50)
				          << "  p95="  << lat_window.percentile(95)
				          << "  p99="  << lat_window.percentile(99)
				          << "  p999=" << lat_window.percentile(99.9)
				          << "  max="  << lat_window.max_ns()
				          << "\n";
				lat_window.reset();
			}

			last_parsed = cur_parsed;
			last_total  = cur_total;
			last_pkts   = cur_pkts;
			last_report = now;
		}
	}

	packet_handler.shutdown();

	const auto run_end = clock::now();
	const double elapsed_s = std::chrono::duration_cast<duration_s>(run_end - last_report).count()
	                         + static_cast<double>(lat_total.total() > 0 ? 1 : 0);  // rough wall time

	std::cout
	    << "\n"
	    << "╔══════════════════════════════════════════════════╗\n"
	    << "║          PIPELINE PROFILING SUMMARY              ║\n"
	    << "╠══════════════════════════════════════════════════╣\n"
	    << "║  Throughput                                      ║\n"
	    << "║    packets received  : " << packet_handler.packets_received()  << "\n"
	    << "║    packets processed : " << packet_handler.packets_processed() << "\n"
	    << "║    msgs total (rx)   : " << md_handler.total_messages()        << "\n"
	    << "║    msgs parsed (ok)  : " << md_handler.messages_processed()    << "\n"
	    << "╠══════════════════════════════════════════════════╣\n"
	    << "║  Latency  (NIC-rx → order-book callback, per pkt)║\n"
	    << "║    samples : " << lat_total.total()           << "\n"
	    << "║    min     : " << lat_total.min_ns()          << " ns\n"
	    << "║    mean    : " << lat_total.mean_ns()         << " ns\n"
	    << "║    p50     : " << lat_total.percentile(50)    << " ns\n"
	    << "║    p75     : " << lat_total.percentile(75)    << " ns\n"
	    << "║    p90     : " << lat_total.percentile(90)    << " ns\n"
	    << "║    p95     : " << lat_total.percentile(95)    << " ns\n"
	    << "║    p99     : " << lat_total.percentile(99)    << " ns\n"
	    << "║    p99.9   : " << lat_total.percentile(99.9)  << " ns\n"
	    << "║    p99.99  : " << lat_total.percentile(99.99) << " ns\n"
	    << "║    max     : " << lat_total.max_ns()          << " ns\n"
	    << "╚══════════════════════════════════════════════════╝\n";
	return 0;
}
