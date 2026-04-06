#!/usr/bin/env python3
"""
ITCH MoldUDP64 Stream Sender
----------------------------
Generates synthetic ITCH 5.0 messages wrapped in MoldUDP64 frames
and sends them as UDP packets to a target server.

Usage:
    python3 itch_sender.py --target <server-ip> --port 12300 --rate 10000

MoldUDP64 Frame format:
    [10 bytes] Session ID (ASCII padded)
    [8  bytes] Sequence Number (big-endian uint64)
    [2  bytes] Message Count (big-endian uint16)
    For each message:
        [2  bytes] Message Length (big-endian uint16)
        [N  bytes] ITCH Message
"""

import argparse
import socket
import struct
import time
import random
import collections


# ── ITCH 5.0 message builders ─────────────────────────────────────────────────
# Struct layout matches ultrahft/market_data/itch_messages.hpp (packed):
#   uint16_t length      – total message length including this field
#   char     message_type
#   uint8_t  reserved
#   uint32_t timestamp   – nanoseconds since midnight
#   ... fields ...

import time as _time

def _ts() -> int:
    """Nanoseconds since midnight."""
    t = _time.time()
    return int((t % 86400) * 1e9) & 0xFFFFFFFF   # fits uint32_t


def make_add_order(order_ref: int, side: str, shares: int,
                   stock: str, price: int) -> bytes:
    """Type 'A' – Add Order.
    Layout: length(2) type(1) reserved(1) timestamp(4)
            order_ref(8) side(1) shares(4) stock(8) price(8) mpid(4)  = 41 bytes
    """
    stock_bytes = stock.encode().ljust(8)[:8]
    body = struct.pack(
        '>QcI8sQI',
        order_ref,
        side.encode(),
        shares,
        stock_bytes,
        price,
        0,           # mpid
    )
    total_len = 2 + 1 + 1 + 4 + len(body)   # length+type+reserved+ts+body
    return struct.pack('>HcBI', total_len, b'A', 0, _ts()) + body


def make_trade(order_ref: int, side: str, shares: int,
               stock: str, price: int, match_num: int) -> bytes:
    """Type 'P' – Trade Message.
    Layout: length(2) type(1) reserved(1) timestamp(4)
            order_ref(8) side(1) shares(4) stock(8) price(8) trade_id(4) cross(1) = 43 bytes
    """
    stock_bytes = stock.encode().ljust(8)[:8]
    body = struct.pack(
        '>QcI8sQIc',
        order_ref,
        side.encode(),
        shares,
        stock_bytes,
        price,
        match_num,
        b'0',        # cross_trade_id type
    )
    total_len = 2 + 1 + 1 + 4 + len(body)
    return struct.pack('>HcBI', total_len, b'P', 0, _ts()) + body


def make_cancel(order_ref: int, cancelled_shares: int) -> bytes:
    """Type 'X' – Order Cancel.
    Layout: length(2) type(1) reserved(1) timestamp(4)
            order_ref(8) cancelled_shares(4)  = 20 bytes
    """
    body = struct.pack('>QI', order_ref, cancelled_shares)
    total_len = 2 + 1 + 1 + 4 + len(body)
    return struct.pack('>HcBI', total_len, b'X', 0, _ts()) + body


# ── MoldUDP64 framing ─────────────────────────────────────────────────────────

SESSION = b'TESTSESS  '  # 10-byte session ID

def build_moldudp64_frame(seq: int, messages: list[bytes]) -> bytes:
    """Wrap a list of ITCH messages in a single MoldUDP64 UDP payload."""
    header = SESSION + struct.pack('>QH', seq, len(messages))
    body = b''.join(struct.pack('>H', len(m)) + m for m in messages)
    return header + body


# ── Sender ────────────────────────────────────────────────────────────────────

STOCKS = ['AAPL    ', 'MSFT    ', 'GOOGL   ', 'AMZN    ', 'NVDA    ']

# Live order tracking: deque gives O(1) append and popleft (FIFO eviction).
# Cap at 50k so FlatOrderMap (262k slots, 70% limit = 183k) never overflows.
LIVE_ORDER_CAP = 50_000
_live_orders: collections.deque = collections.deque()  # deque of (order_ref, shares)
_next_order_ref = 1

def _new_order_ref() -> int:
    global _next_order_ref
    ref = _next_order_ref
    _next_order_ref = (_next_order_ref + 1) & 0xFFFF_FFFF or 1
    return ref

def generate_messages(batch: int) -> list[bytes]:
    msgs = []
    for _ in range(batch):
        stock = random.choice(STOCKS).strip()
        shares = random.randint(100, 10_000)
        price  = random.randint(10_000, 500_000)
        side   = random.choice(['B', 'S'])

        # 50% Add, 30% Cancel existing, 20% Trade existing.
        r = random.random()
        if r < 0.50 or not _live_orders:
            order_ref = _new_order_ref()
            _live_orders.append((order_ref, shares))
            if len(_live_orders) > LIVE_ORDER_CAP:
                _live_orders.popleft()          # O(1) on deque
            msgs.append(make_add_order(order_ref, side, shares, stock, price))
        elif r < 0.80:
            # Swap-remove: swap target with last element, pop last → O(1)
            idx = random.randrange(len(_live_orders))
            # deque doesn't support O(1) random removal, so rotate to front
            _live_orders.rotate(-idx)
            order_ref, orig_shares = _live_orders.popleft()
            _live_orders.rotate(idx)
            msgs.append(make_cancel(order_ref, orig_shares))
        else:
            order_ref, orig_shares = random.choice(_live_orders)  # O(n) but read-only
            msgs.append(make_trade(order_ref, side, orig_shares, stock, price,
                                   random.randint(1, 0xFFFF_FFFF)))
    return msgs


def main():
    parser = argparse.ArgumentParser(description='ITCH MoldUDP64 stream sender')
    parser.add_argument('--target', required=True, help='Target server IP')
    parser.add_argument('--port', type=int, default=12300, help='UDP port (default: 12300)')
    parser.add_argument('--rate', type=int, default=10_000,
                        help='Messages per second (0 = max rate, default: 10000)')
    parser.add_argument('--batch', type=int, default=10,
                        help='Messages per UDP packet (default: 10)')
    parser.add_argument('--duration', type=int, default=0,
                        help='Run for N seconds (0 = forever)')
    parser.add_argument('--burst', type=int, default=64,
                        help='Packets to send per burst before rate-checking (default: 64)')
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 16 * 1024 * 1024)
    dest = (args.target, args.port)

    max_rate = (args.rate == 0)
    packets_per_sec = max(1, args.rate // args.batch) if not max_rate else float('inf')
    interval = 1.0 / packets_per_sec if not max_rate else 0.0

    # Frame pool: pre-packed UDP payloads for the current burst.
    # Rebuilt after every burst so order refs stay realistic without
    # calling struct.pack() inside the send-critical loop.
    POOL_SIZE = args.burst
    frame_pool = [
        build_moldudp64_frame(i * args.batch + 1, generate_messages(args.batch))
        for i in range(POOL_SIZE)
    ]

    seq = 1
    sent_packets = 0
    sent_msgs = 0
    start = time.monotonic()
    last_report = start

    print(f"Sending ITCH stream → {args.target}:{args.port}")
    print(f"Rate: {'MAX' if max_rate else args.rate} msgs/s  |  "
          f"Batch: {args.batch} msgs/pkt  |  Burst: {args.burst} pkts")
    print("Press Ctrl+C to stop.\n")

    try:
        while True:
            burst_start = time.monotonic()

            # Send the pre-packed burst back-to-back — no Python work inside loop.
            for frame in frame_pool:
                sock.sendto(frame, dest)

            sent_packets += POOL_SIZE
            sent_msgs    += POOL_SIZE * args.batch
            seq          += POOL_SIZE * args.batch

            # Rate limiting between bursts.
            if not max_rate:
                burst_duration = time.monotonic() - burst_start
                sleep_time = interval * POOL_SIZE - burst_duration
                if sleep_time > 0.0001:
                    time.sleep(sleep_time)

            # Rebuild pool with fresh order refs for the next burst.
            frame_pool = [
                build_moldudp64_frame(seq + i * args.batch, generate_messages(args.batch))
                for i in range(POOL_SIZE)
            ]

            # Report once per second.
            now = time.monotonic()
            if now - last_report >= 1.0:
                elapsed = now - start
                print(f"  {elapsed:6.1f}s  packets={sent_packets:,}  "
                      f"msgs={sent_msgs:,}  rate={sent_msgs/elapsed:,.0f} msgs/s")
                last_report = now

            if args.duration > 0 and (time.monotonic() - start) >= args.duration:
                break

    except KeyboardInterrupt:
        pass

    elapsed = time.monotonic() - start
    print(f"\nDone. sent {sent_msgs} messages in {elapsed:.1f}s "
          f"({sent_msgs / elapsed:.0f} msgs/s)")
    sock.close()


if __name__ == '__main__':
    main()
