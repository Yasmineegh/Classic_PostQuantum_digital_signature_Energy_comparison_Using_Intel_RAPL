#!/bin/bash

# ==============================================================================
#           System Optimization Script for Performance Measurement
# ==============================================================================
# This script configures the system for stable, low-noise performance
# analysis by disabling dynamic CPU features and isolating workloads.
#
# USAGE: sudo ./setup_environment.sh
#
# WARNING: These changes are intended for temporary benchmark environments.
# They will reduce the general performance and responsiveness of a multi-purpose
# server. A reboot will reset most, but not all, of these settings.
# ==============================================================================

# --- 1. Check for Root Privileges ---
if [ "$EUID" -ne 0 ]; then
  echo "âŒ Error: This script must be run as root. Please use sudo."
  exit 
fi

echo "ðŸš€ Starting system optimization..."

# --- 2. Disable Intel Turbo Boost ---
# By writing '1' to no_turbo, we prevent the CPU from exceeding its base frequency.
# To revert: echo 0 > /sys/devices/system/cpu/intel_pstate/no_turbo
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
  echo "  -> Disabling Intel Turbo Boost..."
  echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
  echo "     Turbo Boost is now disabled."
else
  echo "  -> NOTE: Intel P-State 'no_turbo' file not found. Turbo Boost may be controlled by BIOS."
fi

# --- 3. Disable CPU Frequency Scaling ---
# We set the CPU governor to 'performance' to lock the CPU at its highest
# non-boost frequency, preventing frequency changes during the test.
# To revert: echo "powersave" or "ondemand" to the same files.
echo "  -> Setting CPU governor to 'performance' for all cores..."
for cpu_gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
  echo "performance" > "$cpu_gov"
done
echo "     All cores set to 'performance' mode."

# --- Lock CPU frequency bounds (min=max) ---
if command -v cpupower >/dev/null 2>&1; then
  echo "  -> Locking CPU frequency bounds..."
  cpupower frequency-set -g performance
  cpupower frequency-set -d 2400000
  cpupower frequency-set -u 2400000
  echo "     Frequency locked to ~2.4GHz (if supported)."
else
  echo "  -> cpupower not found; falling back to governor=performance only."
fi

# --- 4. Move Interrupts (IRQs) to Core 0 ---
# This isolates other cores from handling hardware interrupts, reducing jitter.
# We set the smp_affinity for each IRQ to '1', which is the bitmask for Core 0.
# The system will automatically revert this on reboot.
echo "  -> Moving all possible hardware interrupts to Core 0..."
# Iterate over each numeric IRQ
for irq in $(grep -E '^[ 0-9]+:' /proc/interrupts | cut -d: -f1 | tr -d ' '); do
  affinity_file="/proc/irq/$irq/smp_affinity"
  if [ -f "$affinity_file" ]; then
    # The '2>/dev/null' suppresses errors for IRQs that cannot be moved.
    echo 1 > "$affinity_file" 2>/dev/null
  fi
done
echo "     IRQ affinity set."


# --- 6. Disable Address Space Layout Randomization (ASLR) ---
# This setting reverts on reboot. To revert manually: echo 2 > /proc/sys/kernel/randomize_va_space
echo "  -> Disabling Address Space Layout Randomization (ASLR)..."
echo 0 > /proc/sys/kernel/randomize_va_space
echo "     ASLR is now disabled."

# Set consistent memory allocation
echo never > /sys/kernel/mm/transparent_hugepage/enabled

# Disable NUMA balancing
echo 0 > /proc/sys/kernel/numa_balancing
echo -e "\nâœ… System optimization complete. The environment is now configured for benchmarking."

# --- 7. Disable Background Services ---
echo "  -> Disabling non-essential system services..."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DISABLE_SCRIPT="$SCRIPT_DIR/disable_services.sh"

if [ -f "$DISABLE_SCRIPT" ]; then
  chmod +x "$DISABLE_SCRIPT"
  "$DISABLE_SCRIPT"
  echo "     Background services disabled."
else
  echo "     WARNING: disable_services.sh not found in $SCRIPT_DIR"
fi
