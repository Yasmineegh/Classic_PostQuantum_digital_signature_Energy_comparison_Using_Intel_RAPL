#!/bin/bash

# Re-enable services after benchmarking

if [ "$EUID" -ne 0 ]; then
  echo "❌ Error: This script must be run as root. Please use sudo."
  exit 1
fi

GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

STATE_FILE="/tmp/benchmark_services_state.txt"

if [ ! -f "$STATE_FILE" ]; then
    echo "❌ No saved state file found. Services may not have been disabled by disable_services.sh"
    exit 1
fi

echo -e "${BLUE}🔄 Re-enabling services...${NC}"
echo ""

while IFS=: read -r service state; do
    if [ "$service" = "swap" ]; then
        echo -e "${GREEN}[✓]${NC} Re-enabling swap..."
        swapon -a 2>/dev/null || true
        continue
    fi
    
    if [ "$state" = "masked" ]; then
        systemctl unmask "$service" 2>/dev/null
        systemctl start "$service" 2>/dev/null
        echo -e "${GREEN}[✓]${NC} Unmasked and started $service"
    elif [ "$state" = "active" ]; then
        systemctl enable "$service" 2>/dev/null
        systemctl start "$service" 2>/dev/null
        echo -e "${GREEN}[✓]${NC} Re-enabled and started $service"
    elif [ "$state" = "enabled" ]; then
        systemctl enable "$service" 2>/dev/null
        echo -e "${GREEN}[✓]${NC} Re-enabled $service"
    fi
done < "$STATE_FILE"

# Restore journald configuration
if [ -f /etc/systemd/journald.conf.d/benchmark.conf ]; then
    rm /etc/systemd/journald.conf.d/benchmark.conf
    systemctl restart systemd-journald
    echo -e "${GREEN}[✓]${NC} Restored journald configuration"
fi

# Restore writeback
echo 500 > /proc/sys/vm/dirty_writeback_centisecs 2>/dev/null || true

echo ""
echo -e "${GREEN}✅ Services restored to normal operation${NC}"
echo ""

rm "$STATE_FILE"
