#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────
#  UltraHFT DPDK Setup Script
#  Run once after every reboot before starting market_data_pipeline_demo
# ─────────────────────────────────────────────────────────────
set -euo pipefail

NIC_PCI="0000:06:00.0"
NIC_IFACE="eno1"
NIC_IP="10.0.0.1/24"
DRIVER="vfio-pci"
HUGEPAGES=1024

echo "[0/6] Clearing stale DPDK runtime files..."
sudo rm -f /dev/hugepages/rtemap_* 2>/dev/null || true
rm -f /run/user/"$(id -u)"/dpdk/rte/* 2>/dev/null || true
echo "  OK"

echo "[1/6] Checking hugepages..."
CURRENT=$(cat /proc/sys/vm/nr_hugepages)
if [ "$CURRENT" -lt "$HUGEPAGES" ]; then
    echo "  Allocating $HUGEPAGES hugepages..."
    echo "$HUGEPAGES" | sudo tee /proc/sys/vm/nr_hugepages > /dev/null
else
    echo "  OK ($CURRENT hugepages already allocated)"
fi

echo "[2/6] Mounting hugepages with user permissions..."
if mountpoint -q /dev/hugepages; then
    sudo umount /dev/hugepages
fi
sudo mount -t hugetlbfs -o uid="$(id -u)",gid="$(id -g)",mode=775 nodev /dev/hugepages
echo "  OK"

echo "[3/6] Assigning static IP $NIC_IP to $NIC_IFACE (for ARP/sender targeting)..."
sudo ip addr add "$NIC_IP" dev "$NIC_IFACE" 2>/dev/null || true
sudo ip link set "$NIC_IFACE" up
echo "  eno1 MAC: $(cat /sys/class/net/$NIC_IFACE/address)"
echo "  eno1 IP : $(echo $NIC_IP | cut -d/ -f1)"

echo "[4/6] Binding $NIC_PCI to $DRIVER..."
sudo modprobe vfio-pci
sudo ip link set "$NIC_IFACE" down 2>/dev/null || true
sudo dpdk-devbind.py --bind="$DRIVER" "$NIC_PCI"
echo "  OK"

echo "[5/6] Fixing VFIO group permissions..."
VFIO_GROUP=$(ls /dev/vfio/ | grep -v "^vfio$" | grep -v "^devices$" | head -1)
sudo chown "$(id -u):$(id -g)" "/dev/vfio/$VFIO_GROUP"
echo "  /dev/vfio/$VFIO_GROUP → owned by $(id -un)"

echo "[6/6] Opening firewall for UDP port 12300..."
sudo ufw allow 12300/udp > /dev/null 2>&1 && echo "  OK" || echo "  (ufw not active, skipping)"

echo ""
echo "══════════════════════════════════════════════════════"
echo "  DPDK setup complete!"
echo "  Server NIC IP  : $(echo $NIC_IP | cut -d/ -f1)"
echo "  Server NIC MAC : $(cat /sys/class/net/eno1/address 2>/dev/null || echo 'bc:fc:e7:e7:c1:d7')"
echo ""
echo "  Run receiver:"
echo "    cd /home/wqi/UltraHFT/build && ./market_data_pipeline_demo"
echo ""
echo "  Run sender on OTHER PC:"
echo "    python3 tools/itch_sender.py \\"
echo "      --target $(echo $NIC_IP | cut -d/ -f1) \\"
echo "      --port 12300 --rate 100000 --batch 10"
echo "══════════════════════════════════════════════════════"
