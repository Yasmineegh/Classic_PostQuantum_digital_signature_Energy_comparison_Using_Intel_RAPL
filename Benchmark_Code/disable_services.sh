#!/bin/bash

# ==============================================================================
#                   Disable Non-Essential Services for Benchmarking
# ==============================================================================
# This script disables common background services that can interfere with
# performance measurements. Services will be re-enabled after benchmarking.
#
# USAGE: sudo ./disable_services.sh
#
# WARNING: This will temporarily disable:
# - Automatic updates
# - System monitoring
# - Network services (some)
# - Scheduled tasks
# - Desktop effects (if applicable)
#
# Run enable_services.sh after benchmarking to restore normal operation.
# ==============================================================================

# Check for root
if [ "$EUID" -ne 0 ]; then
  echo "❌ Error: This script must be run as root. Please use sudo."
  exit 1
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log() { echo -e "${BLUE}[$(date +'%H:%M:%S')]${NC} $1"; }
success() { echo -e "${GREEN}[✓]${NC} $1"; }
warning() { echo -e "${YELLOW}[!]${NC} $1"; }
info() { echo -e "${CYAN}[i]${NC} $1"; }

# Save current service states
STATE_FILE="/tmp/benchmark_services_state.txt"
> "$STATE_FILE"

# Function to safely stop and mask a service
mask_service() {
    local service=$1
    local description=$2
    
    if systemctl is-active --quiet "$service" 2>/dev/null || \
       systemctl is-enabled --quiet "$service" 2>/dev/null; then
        echo "$service:masked" >> "$STATE_FILE"
        systemctl stop "$service" 2>/dev/null
        systemctl mask "$service" 2>/dev/null && \
        success "Masked $description ($service)" || \
        warning "Could not mask $service"
    fi
}
# Function to safely disable (not mask) a unit
disable_service() {
    local unit=$1
    local description=$2

    if systemctl list-unit-files | grep -q "^${unit}"; then
        echo "$unit:disabled" >> "$STATE_FILE"
        systemctl stop "$unit" 2>/dev/null
        systemctl disable "$unit" 2>/dev/null && \
        success "Disabled $description ($unit)" || \
        warning "Could not disable $unit"
    fi
}


echo "🛑 Disabling non-essential services for benchmarking..."
echo ""

# ==================== AUTOMATIC UPDATES ====================
log "Disabling automatic updates and package management..."
mask_service "apt-daily.timer" "APT daily updates"
mask_service "apt-daily-upgrade.timer" "APT daily upgrades"
mask_service "unattended-upgrades" "Unattended upgrades"
mask_service "packagekit" "PackageKit"
mask_service "apt-daily.service" "APT daily service"
mask_service "apt-daily-upgrade.service" "APT daily upgrade service"
echo ""

# ==================== SYSTEM MONITORING ====================
log "Disabling system monitoring services..."
mask_service "apport" "Crash reporting"
mask_service "whoopsie" "Ubuntu error reporting"
mask_service "kerneloops" "Kernel oops monitoring"
mask_service "systemd-oomd" "Out-of-memory daemon"
echo ""

# ==================== LOGGING & JOURNALING ====================
log "Reducing logging overhead..."
mask_service "rsyslog" "System logging"
# Note: We keep journald running but rate-limit it
if systemctl is-active --quiet systemd-journald; then
    info "Rate-limiting systemd-journald (keeping it running)"
    mkdir -p /etc/systemd/journald.conf.d/
    cat > /etc/systemd/journald.conf.d/benchmark.conf << 'EOF'
[Journal]
RateLimitIntervalSec=0
RateLimitBurst=0
Storage=volatile
RuntimeMaxUse=50M
EOF
    systemctl restart systemd-journald
    success "Configured journald for minimal overhead"
fi
echo ""

# ==================== SCHEDULED TASKS ====================
log "Disabling scheduled tasks and timers..."
mask_service "cron" "Cron daemon"
mask_service "anacron" "Anacron"
mask_service "atd" "AT daemon"
mask_service "systemd-tmpfiles-clean.timer" "Temporary files cleanup"
mask_service "man-db.timer" "Manual database updates"
mask_service "fstrim.timer" "SSD TRIM operations"
mask_service "logrotate.timer" "Log rotation"
mask_service "motd-news.timer" "Message of the day updates"
echo ""

# ==================== NETWORK SERVICES ====================
log "Disabling unnecessary network services..."
mask_service "avahi-daemon" "Avahi mDNS/DNS-SD"
mask_service "bluetooth" "Bluetooth"
mask_service "ModemManager" "Modem management"
mask_service "NetworkManager-dispatcher" "NetworkManager dispatcher"
mask_service "wpa_supplicant" "WPA supplicant"

# Optional: Completely disable network (uncomment if you don't need it)
# warning "Optionally disabling all network services..."
# systemctl stop NetworkManager 2>/dev/null
# systemctl stop networking 2>/dev/null
# echo "NetworkManager:optional" >> "$STATE_FILE"
echo ""

# ==================== DESKTOP & GUI SERVICES ====================
log "Disabling desktop environment services..."
mask_service "accounts-daemon" "Accounts service"
mask_service "cups" "Printing service"
mask_service "cups-browsed" "CUPS browsing"
mask_service "udisks2" "Disk management"
mask_service "upower" "Power management"
mask_service "thermald" "Thermal daemon"
mask_service "colord" "Color management"
mask_service "geoclue" "Geolocation service"
mask_service "speech-dispatcher" "Speech dispatcher"
echo ""

# ==================== VIRTUALIZATION & CONTAINERS ====================
log "Disabling virtualization services..."
mask_service "docker" "Docker"
mask_service "containerd" "Containerd"
mask_service "libvirtd" "Libvirt"
mask_service "virtlogd" "Libvirt logging"
echo ""

# ==================== DATABASE & WEB SERVICES ====================
log "Disabling database and web services..."
mask_service "mysql" "MySQL"
mask_service "mariadb" "MariaDB"
mask_service "postgresql" "PostgreSQL"
mask_service "apache2" "Apache web server"
mask_service "nginx" "Nginx web server"
mask_service "redis" "Redis"
mask_service "mongodb" "MongoDB"
echo ""

mask_service "cryptdisks-early.service" "Encrypted disk setup (early)"
mask_service "cryptdisks.service" "Encrypted disk setup"
mask_service "hwclock.service" "Hardware clock sync"
mask_service "irqbalance.service" "IRQ balancing daemon"
mask_service "multipath-tools-boot.service" "Multipath boot tools"
mask_service "multipath-tools.service" "Multipath tools"
mask_service "sudo.service" "sudo service"
mask_service "x11-common.service" "X11 common scripts"

disable_service "console-getty.service" "Console getty"
disable_service "debug-shell.service" "Debug shell"
disable_service "iscsid.service" "iSCSI daemon"
disable_service "pg_receivewal@.service" "Postgres WAL receiver template"
disable_service "pollinate.service" "Cloud/entropy pollination"
disable_service "serial-getty@.service" "Serial getty template"
disable_service "sysstat.service" "System statistics collection"
disable_service "systemd-networkd-wait-online@.service" "Network wait-online (template)"


# ==================== ADDITIONAL OPTIMIZATIONS ====================
log "Applying additional optimizations..."

# Disable swap (reduces I/O interference)
if [ -n "$(swapon --show)" ]; then
    info "Disabling swap..."
    swapoff -a && success "Swap disabled" || warning "Could not disable swap"
    echo "swap:active" >> "$STATE_FILE"
fi

# ==================== SUMMARY ====================
log "📊 Service Disable Summary"
echo ""
info "Service states saved to: $STATE_FILE"
info "To restore services after benchmarking, run: sudo ./enable_services.sh"
echo ""
success "System is now optimized for benchmarking!"
echo ""

# Show what's still running
log "Currently active services (filtered):"
systemctl list-units --type=service --state=running --no-pager | \
    grep -v "systemd-\|user@\|dbus\|ssh" | \
    head -20
echo ""

# Show process count per core
log "Process distribution after optimization:"
for cpu in 0 1 2 3; do
    count=$(ps -eLo psr 2>/dev/null | grep "^[[:space:]]*$cpu$" | wc -l || echo "0")
    if [ $cpu -eq 0 ]; then
        echo "  Core $cpu (system): $count threads"
    else
        echo "  Core $cpu (isolated): $count threads"
    fi
done
echo ""

warning "IMPORTANT: Some services are now disabled!"
warning "After benchmarking, run: sudo ./enable_services.sh"
echo ""
#!/bin/bash

# Additional service disabling for minimal interference

echo "Disabling cloud-init services..."
systemctl stop cloud-init-local.service 2>/dev/null
systemctl stop cloud-init.service 2>/dev/null
systemctl stop cloud-config.service 2>/dev/null
systemctl stop cloud-final.service 2>/dev/null
systemctl mask cloud-init-local.service 2>/dev/null
systemctl mask cloud-init.service 2>/dev/null
systemctl mask cloud-config.service 2>/dev/null
systemctl mask cloud-final.service 2>/dev/null

echo "Disabling additional monitoring/management services..."
systemctl stop multipathd.service 2>/dev/null
systemctl stop lvm2-monitor.service 2>/dev/null
systemctl stop networkd-dispatcher.service 2>/dev/null
systemctl stop open-iscsi.service 2>/dev/null
systemctl stop pollinate.service 2>/dev/null
systemctl stop lvm-sensors.service 2>/dev/null

echo "✓ Additional services disabled"
# Create the enable script
cat > enable_services.sh << 'ENABLE_EOF'
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


echo ""
echo -e "${GREEN}✅ Services restored to normal operation${NC}"
echo ""

rm "$STATE_FILE"
ENABLE_EOF

chmod +x enable_services.sh
success "Created enable_services.sh for restoring services later"
echo ""

info "Next steps:"
echo "  1. Run your benchmarks: sudo ./run_benchmarks.sh"
echo "  2. After completion: sudo ./enable_services.sh"
echo ""