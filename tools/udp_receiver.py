#!/usr/bin/env python3
"""
Fallback UDP ITCH Receiver (no DPDK)
--------------------------------------
Listens on a UDP port and parses MoldUDP64 / ITCH messages.
Use this to test the ITCH stream from itch_sender.py over WiFi,
without needing DPDK or a dedicated NIC.

Usage:
    python3 udp_receiver.py --port 12300
"""

import argparse
import socket
import struct
import time
from collections import defaultdict


# ── MoldUDP64 / ITCH parser ───────────────────────────────────────────────────

def read_be16(data: bytes, off: int) -> int:
    return struct.unpack_from('>H', data, off)[0]

def read_be32(data: bytes, off: int) -> int:
    return struct.unpack_from('>I', data, off)[0]

def read_be64(data: bytes, off: int) -> int:
    return struct.unpack_from('>Q', data, off)[0]

MSG_TYPES = {
    b'A': 'AddOrder',
    b'P': 'Trade',
    b'X': 'Cancel',
    b'D': 'Delete',
    b'E': 'Execute',
    b'F': 'ExecuteWithPrice',
    b'U': 'Replace',
    b'S': 'SystemEvent',
    b'R': 'StockDirectory',
    b'H': 'TradingAction',
    b'Y': 'RegSHO',
    b'L': 'MarketParticipant',
    b'V': 'MWCB',
    b'W': 'MWCBStatus',
    b'K': 'IPO',
    b'J': 'LULD',
    b'h': 'OperationalHalt',
    b'Q': 'CrossTrade',
    b'B': 'BrokenTrade',
    b'I': 'NOII',
    b'N': 'RPII',
}

def parse_moldudp64(payload: bytes) -> list[bytes]:
    """Extract raw ITCH messages from a MoldUDP64 frame."""
    if len(payload) < 20:
        return []
    # session(10) + seq(8) + count(2)
    msg_count = read_be16(payload, 18)
    messages = []
    off = 20
    for _ in range(msg_count):
        if off + 2 > len(payload):
            break
        msg_len = read_be16(payload, off)
        off += 2
        if msg_len == 0 or off + msg_len > len(payload):
            break
        messages.append(payload[off:off + msg_len])
        off += msg_len
    return messages


def parse_itch_message(msg: bytes) -> dict:
    if not msg:
        return {}
    msg_type = msg[0:1]
    name = MSG_TYPES.get(msg_type, f'Unknown({msg_type})')
    info = {'type': name, 'raw_type': msg_type.decode(errors='replace'), 'len': len(msg)}

    if msg_type == b'A' and len(msg) >= 36:
        info['side'] = chr(msg[9])
        info['shares'] = read_be32(msg, 10)
        info['stock'] = msg[14:22].decode(errors='replace').strip()
        info['price'] = read_be32(msg, 22) / 10000.0
    elif msg_type == b'P' and len(msg) >= 40:
        info['side'] = chr(msg[9])
        info['shares'] = read_be32(msg, 10)
        info['stock'] = msg[14:22].decode(errors='replace').strip()
        info['price'] = read_be32(msg, 22) / 10000.0
    elif msg_type == b'X' and len(msg) >= 23:
        info['cancelled'] = read_be32(msg, 19)

    return info


# ── Main receiver ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='UDP ITCH receiver (no DPDK)')
    parser.add_argument('--port', type=int, default=12300, help='UDP port to listen on')
    parser.add_argument('--bind', default='0.0.0.0', help='Bind address (default: all interfaces)')
    parser.add_argument('--verbose', action='store_true', help='Print every message')
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    sock.bind((args.bind, args.port))
    sock.settimeout(1.0)

    print(f"Listening on {args.bind}:{args.port} for ITCH/MoldUDP64 stream...")
    print("Send packets with:  python3 itch_sender.py --target <THIS_IP> --port "
          f"{args.port} --rate 10000\n")

    counters = defaultdict(int)
    packets = 0
    messages = 0
    start = time.monotonic()
    last_report = start

    try:
        while True:
            try:
                data, addr = sock.recvfrom(65535)
            except socket.timeout:
                continue

            packets += 1
            msgs = parse_moldudp64(data)

            for msg in msgs:
                messages += 1
                info = parse_itch_message(msg)
                counters[info.get('raw_type', '?')] += 1

                if args.verbose:
                    print(f"  [{addr[0]}] {info}")

            now = time.monotonic()
            if now - last_report >= 1.0:
                elapsed = now - start
                rate = messages / elapsed if elapsed > 0 else 0
                print(f"  {elapsed:6.1f}s | pkts={packets:,} msgs={messages:,} "
                      f"rate={rate:,.0f}/s | "
                      + " ".join(f"{MSG_TYPES.get(k.encode(), k)}={v}"
                                 for k, v in sorted(counters.items())))
                last_report = now

    except KeyboardInterrupt:
        pass

    elapsed = time.monotonic() - start
    print(f"\nStopped after {elapsed:.1f}s")
    print(f"  Total packets : {packets:,}")
    print(f"  Total messages: {messages:,}")
    print(f"  Avg rate      : {messages/elapsed:,.0f} msgs/s")
    print(f"  By type       : { {MSG_TYPES.get(k.encode(), k): v for k, v in counters.items()} }")
    sock.close()


if __name__ == '__main__':
    main()
