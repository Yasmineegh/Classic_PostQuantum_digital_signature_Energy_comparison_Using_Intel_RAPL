#!/bin/bash

# ==============================================================================
#                   Multi-Algorithm Cryptographic Benchmark Suite
# ==============================================================================
# This script automates benchmarking of multiple cryptographic algorithms:
# RSA, ECDSA, EdDSA, Dilithium, Falcon, and SPHINCS+ at NIST Security Level 1
#
# Security Level: Level 1 (~128-bit equivalent)
# - RSA: 3072-bit (with PSS padding, SHA-256)
# - ECDSA: P-256 curve (SHA-256)
# - EdDSA: Ed25519 (SHA-512 built-in)
# - Dilithium: Dilithium2 (SHAKE256)
# - Falcon: Falcon-512 (SHAKE256)
# - SPHINCS+: SPHINCS+-SHA2-128f (SHA-256)
#
# Each algorithm runs 5 times in random order with 300s cooling between runs
# Benchmarks run on isolated cores 1-2
#
# USAGE: sudo ./run_benchmarks.sh
# NOTE: This script requires sudo privileges for RAPL access and system setup
# ==============================================================================

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

# CPU affinity configuration - use isolated cores 1-2
CPU_CORES="1,2"

# Algorithm configurations at NIST Security Level 1 (~128-bit)
declare -A ALGORITHMS=(
    ["rsa"]="3072"
    ["ecdsa"]="P-256"
    ["eddsa"]="Ed25519"
    ["dilithium"]="Dilithium2"
    ["falcon"]="Falcon-512"
    ["sphincs_f"]="SPHINCS+-SHA2-128f"
    ["sphincs_s"]="SPHINCS+-SHA2-128s"
)

# Algorithm display names
declare -A ALGO_NAMES=(
    ["rsa"]="RSA-3072"
    ["ecdsa"]="ECDSA P-256"
    ["eddsa"]="EdDSA Ed25519"
    ["dilithium"]="Dilithium2"
    ["falcon"]="Falcon-512"
    ["sphincs_f"]="SPHINCS+-SHA2-128f"
    ["sphincs_s"]="SPHINCS+-SHA2-128s"
)

# Algorithm source files
declare -A SOURCE_FILES=(
    ["rsa"]="rsa_benchmark.c"
    ["ecdsa"]="ecdsa_benchmark.c"
    ["eddsa"]="eddsa_benchmark.c"
    ["dilithium"]="dilithium_benchmark.c"
    ["falcon"]="falcon_benchmark.c"
    ["sphincs_f"]="sphincs_benchmark.c"
    ["sphincs_s"]="sphincs_benchmark.c"
)

# Algorithm binary names
declare -A BINARIES=(
    ["rsa"]="rsa_benchmark"
    ["ecdsa"]="ecdsa_benchmark"
    ["eddsa"]="eddsa_benchmark"
    ["dilithium"]="dilithium_benchmark"
    ["falcon"]="falcon_benchmark"
    ["sphincs_f"]="sphincs_benchmark"
    ["sphincs_s"]="sphincs_benchmark"
)

# Cooling periods (seconds)
INTER_RUN_COOLING=300     # Between each run (now 300s as requested)
SYSTEM_STABILIZATION=120  # Initial system stabilization

# Logging functions
log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

progress() {
    echo -e "${PURPLE}[PROGRESS]${NC} $1"
}

# Check if running as root
check_root() {
    if [ "$EUID" -ne 0 ]; then
        error "This script must be run as root for RAPL access and system optimization."
        echo "Usage: sudo ./run_benchmarks.sh"
        exit 1
    fi
}

# Load RAPL kernel modules
load_rapl_modules() {
    log "Loading RAPL kernel modules..."
    
    # Load intel_rapl_common (dependency)
    if ! lsmod | grep -q "intel_rapl_common"; then
        log "Loading intel_rapl_common module..."
        modprobe intel_rapl_common || {
            error "Failed to load intel_rapl_common module"
            exit 1
        }
    else
        info "intel_rapl_common already loaded"
    fi
    
    # Load intel_rapl_msr (main RAPL interface)
    if ! lsmod | grep -q "intel_rapl_msr"; then
        log "Loading intel_rapl_msr module..."
        modprobe intel_rapl_msr || {
            error "Failed to load intel_rapl_msr module"
            exit 1
        }
    else
        info "intel_rapl_msr already loaded"
    fi
    
    # Verify RAPL interfaces are available
    if [ -d "/sys/class/powercap/intel-rapl" ]; then
        success "RAPL interfaces are available"
        ls -la /sys/class/powercap/intel-rapl/ | head -5
    else
        error "RAPL interfaces not found. Your CPU may not support RAPL."
        exit 1
    fi
}

# Run system optimization
run_setup() {
    log "Running system optimization setup..."
    
    if [ ! -f "setup.sh" ]; then
        error "setup.sh not found in current directory"
        exit 1
    fi
    
    chmod +x setup.sh
    ./setup.sh || {
        error "System setup failed"
        exit 1
    }
    
    success "System optimization completed"
}

# Check for liboqs library availability
check_liboqs() {
    log "Checking for liboqs library..."
    
    # Check if liboqs is installed
    if ! pkg-config --exists liboqs; then
        error "liboqs library not found. Post-quantum algorithms require liboqs."
        error "Please install liboqs library:"
        error "  Ubuntu/Debian: sudo apt install liboqs-dev"
        error "  Or build from source: https://github.com/open-quantum-safe/liboqs"
        return 1
    fi
    
    # Get liboqs compiler flags
    OQS_CFLAGS=$(pkg-config --cflags liboqs)
    OQS_LIBS=$(pkg-config --libs liboqs)
    
    return 0
} 

# Check and verify source files
check_source_files() {
    log "Checking source files availability..."
    
    local missing_files=()
    
    # Check common files
    local common_files=("benchmark_framework.c" "power_monitoring.c")
    for file in "${common_files[@]}"; do
        if [ ! -f "$file" ]; then
            missing_files+=("$file")
        fi
    done
    
    # Check algorithm-specific files
    local unique_sources=(
        "rsa_benchmark.c"
        "ecdsa_benchmark.c"
        "eddsa_benchmark.c"
        "dilithium_benchmark.c"
        "falcon_benchmark.c"
        "sphincs_benchmark.c"
    )
    
    for file in "${unique_sources[@]}"; do
        if [ ! -f "$file" ]; then
            missing_files+=("$file")
        fi
    done
    
    if [ ${#missing_files[@]} -gt 0 ]; then
        error "Missing required source files:"
        printf '  - %s\n' "${missing_files[@]}"
        exit 1
    fi
    
    success "All source files are available"
}

# Compile a specific algorithm
compile_algorithm() {
    local algo=$1
    local source_file="${SOURCE_FILES[$algo]}"
    local binary="${BINARIES[$algo]}"
    
    # Skip if already compiled
    if [ -x "./$binary" ]; then
        info "${ALGO_NAMES[$algo]} binary already compiled"
        return 0
    fi
    
    log "Compiling ${ALGO_NAMES[$algo]} benchmark..."
    
    # Common compilation flags and libraries
    local common_flags="-O2 -Wall -Wextra -std=c99"
    local common_libs="-lssl -lcrypto -lpthread -lm"
    
    # Algorithm-specific compilation
    case $algo in
        "rsa"|"ecdsa"|"eddsa")
            gcc $common_flags \
                benchmark_framework.c \
                power_monitoring.c \
                "$source_file" \
                -o "$binary" \
                $common_libs || {
                error "Compilation failed for $algo"
                return 1
            }
            ;;
        "dilithium"|"falcon"|"sphincs_f"|"sphincs_s")
            # Post-quantum algorithms need liboqs
            gcc $common_flags $OQS_CFLAGS \
                benchmark_framework.c \
                power_monitoring.c \
                "$source_file" \
                -o "$binary" \
                $common_libs $OQS_LIBS || {
                error "Compilation failed for $algo"
                error "Make sure liboqs is properly installed and configured"
                return 1
            }
            ;;
        *)
            error "Unknown algorithm: $algo"
            return 1
            ;;
    esac
    
    success "Successfully compiled ${ALGO_NAMES[$algo]}"
}

# Compile all algorithms
compile_all() {
    log "Compiling all cryptographic benchmark algorithms..."
    
    # Check for liboqs before compiling post-quantum algorithms
    local has_liboqs=false
    if check_liboqs; then
        has_liboqs=true
    else
        warning "liboqs not available. Post-quantum algorithms will be skipped."
        warning "Only RSA, ECDSA, and EdDSA will be benchmarked."
    fi
    
    local failed_compilations=()
    local skipped_algorithms=()
    
    for algo in "${!ALGORITHMS[@]}"; do
        # Skip post-quantum algorithms if liboqs is not available
        if [[ "$algo" =~ ^(dilithium|falcon|sphincs_f|sphincs_s) ]] && [ "$has_liboqs" = false ]; then
            skipped_algorithms+=("$algo")
            warning "Skipping ${ALGO_NAMES[$algo]} - liboqs not available"
            continue
        fi
        
        if ! compile_algorithm "$algo"; then
            failed_compilations+=("$algo")
        fi
    done
    
    if [ ${#skipped_algorithms[@]} -gt 0 ]; then
        warning "Skipped algorithms due to missing liboqs:"
        printf '  - %s\n' "${skipped_algorithms[@]}"
    fi
    
    if [ ${#failed_compilations[@]} -gt 0 ]; then
        error "Failed to compile the following algorithms:"
        printf '  - %s\n' "${failed_compilations[@]}"
        exit 1
    fi
    
    success "All available algorithms compiled successfully"
}

# System cooling period with progress indicator
cooling_period() {
    local duration=$1
    local description=$2
    
    log "$description (${duration}s cooling period)"
    
    sleep "$duration"
    
    success "$description complete"
}

# Run a single benchmark for a specific algorithm
run_single_benchmark() {
    local algo=$1
    local run_number=$2
    local total_runs=$3
    
    local binary="${BINARIES[$algo]}"
    local algo_name="${ALGO_NAMES[$algo]}"
    local param="${ALGORITHMS[$algo]}"
    
    progress "Running $algo_name benchmark (Run ${run_number}/${total_runs})"
    
    # Create timestamped log file
    local timestamp=$(date +'%Y%m%d_%H%M%S')
    local run_log="${algo}_run${run_number}_${timestamp}.log"
    
    # Verify binary exists and is executable
    if [ ! -x "./$binary" ]; then
        error "Binary not found or not executable: $binary"
        return 1
    fi
    
    # Run the benchmark with the appropriate parameter using taskset for CPU affinity
    log "Executing on cores ${CPU_CORES}: taskset -c ${CPU_CORES} ./$binary $param"
    if taskset -c ${CPU_CORES} ./"$binary" "$param" 2>&1 | tee "$run_log"; then
        success "$algo_name Run ${run_number} completed successfully"
        info "Output logged to: $run_log"
    else
        error "$algo_name Run ${run_number} failed"
        return 1
    fi
    
    return 0
}

# Create randomized run order (each algorithm appears 5 times)
create_random_run_order() {
    local -n run_array=$1
    
    # Clear array
    run_array=()
    
    # Get list of available algorithms
    local available_algos=()
    for algo in "${!ALGORITHMS[@]}"; do
        local binary="${BINARIES[$algo]}"
        if [ -x "./$binary" ]; then
            available_algos+=("$algo")
        fi
    done
    
    # Add each algorithm 5 times
    for algo in "${available_algos[@]}"; do
        for i in {1..5}; do
            run_array+=("$algo")
        done
    done
    
    # Shuffle the array using Fisher-Yates algorithm
    local n=${#run_array[@]}
    for ((i=n-1; i>0; i--)); do
        local j=$((RANDOM % (i+1)))
        local temp="${run_array[i]}"
        run_array[i]="${run_array[j]}"
        run_array[j]="$temp"
    done
}

# Run all benchmarks in random order
run_all_benchmarks() {
    local run_order=()
    
    # Create randomized run order
    create_random_run_order run_order
    
    local total_runs=${#run_order[@]}
    
    if [ "$total_runs" -eq 0 ]; then
        error "No compiled binaries available for benchmarking"
        exit 1
    fi
    
    log "Starting Multi-Algorithm Cryptographic Benchmark Suite"
    log "======================================================"
    info "Total benchmark runs: ${total_runs} (each algorithm runs 5 times)"
    info "Algorithms: 7 (RSA, ECDSA, EdDSA, Dilithium, Falcon, SPHINCS+-SHA2-128f, SPHINCS+-SHA2-128s)"
    info "Security level: NIST Level 1 (~128-bit equivalent)"
    info "CPU Affinity: Cores ${CPU_CORES} (isolated)"
    info "Cooling period between runs: ${INTER_RUN_COOLING}s"
    echo
    
    # Display configuration summary
    log "Algorithm Configuration Summary:"
    for algo in "${!ALGORITHMS[@]}"; do
        local binary="${BINARIES[$algo]}"
        if [ -x "./$binary" ]; then
            printf "  %-25s: %s\n" "${ALGO_NAMES[$algo]}" "${ALGORITHMS[$algo]}"
        fi
    done
    echo
    
    # Display randomized run order
    log "Randomized Run Order:"
    for ((i=0; i<total_runs; i++)); do
        printf "  Run %2d: %s\n" "$((i+1))" "${ALGO_NAMES[${run_order[i]}]}"
    done
    echo
    
    # Initial system stabilization
    cooling_period $SYSTEM_STABILIZATION "Initial system stabilization"
    echo
    
    # Execute runs in random order
    for ((i=0; i<total_runs; i++)); do
        local algo="${run_order[i]}"
        local run_num=$((i+1))
        
        progress "Executing run ${run_num}/${total_runs}: ${ALGO_NAMES[$algo]}"
        
        if ! run_single_benchmark "$algo" "$run_num" "$total_runs"; then
            error "Failed to complete run ${run_num} for ${ALGO_NAMES[$algo]}"
            exit 1
        fi
        
        # Cooling period between runs (except for the last one)
        if [ "$run_num" -lt "$total_runs" ]; then
            cooling_period $INTER_RUN_COOLING "Cooling between runs"
            echo
        fi
    done
}

# Generate comprehensive summary report
generate_summary() {
    log "Generating comprehensive benchmark summary..."
    
    local summary_file="crypto_benchmark_summary_$(date +'%Y%m%d_%H%M%S').txt"
    local timestamp=$(date)
    local system_info=$(uname -a)
    
    cat > "$summary_file" << EOF
Multi-Algorithm Cryptographic Benchmark Summary Report
======================================================
Generated: $timestamp
System: $system_info

Benchmark Configuration:
=======================
Security Level: NIST Level 1 (~128-bit equivalent)
CPU Affinity: Cores ${CPU_CORES} (isolated for benchmarking)

Algorithm Parameters:
- RSA: 3072-bit keys with PSS padding, SHA-256
- ECDSA: P-256 curve (NIST curve), SHA-256
- EdDSA: Ed25519, SHA-512 (built-in)
- Dilithium: Dilithium2 (Level 1 security), SHAKE256
- Falcon: Falcon-512 (Level 1 security), SHAKE256
- SPHINCS+: SPHINCS+-SHA2-128f (fast variant), SHA-256
- SPHINCS+: SPHINCS+-SHA2-128s (small variant), SHA-256

Execution Parameters:
- Total runs: 5 per algorithm (randomized order)
- Benchmark duration: 60 seconds per operation type
- Warmup duration: 240 seconds per operation type
- Cooldown duration: 240 seconds between phases
- Inter-run cooling: ${INTER_RUN_COOLING}s
- Initial stabilization: ${SYSTEM_STABILIZATION}s

System Optimizations Applied:
============================
- Intel Turbo Boost: Disabled
- CPU Governor: Performance mode
- IRQ Affinity: Moved to Core 0
- Tuning Profile: latency-performance
- ASLR: Disabled
- NUMA Balancing: Disabled
- RAPL Power Monitoring: Enabled
- Process CPU Affinity: Cores ${CPU_CORES} (isolated)

Generated Files:
===============
EOF

    # List all generated log files by algorithm
    echo "Individual Run Logs:" >> "$summary_file"
    for algo in "${!ALGORITHMS[@]}"; do
        echo "  ${ALGO_NAMES[$algo]}:" >> "$summary_file"
        ls -la ${algo}_run*_*.log 2>/dev/null | sed 's/^/    /' >> "$summary_file" || echo "    No log files found" >> "$summary_file"
    done
    echo "" >> "$summary_file"
    
    # List CSV data directories
    echo "Data Directories:" >> "$summary_file"
    for algo in "${!ALGORITHMS[@]}"; do
        if [ -d "$algo/" ]; then
            echo "  ${ALGO_NAMES[$algo]} data: $algo/" >> "$summary_file"
            ls -la "$algo/" | head -5 | sed 's/^/    /' >> "$summary_file"
        fi
    done
    
    # Add performance comparison note
    cat >> "$summary_file" << EOF

Analysis Notes:
==============
- All algorithms tested at NIST Security Level 1 (~128-bit equivalent)
- Each algorithm ran 5 times in randomized order to minimize systematic bias
- 300s cooling period between each run to ensure thermal stability
- This is the baseline security level suitable for most applications
- RSA-3072 provides equivalent classical security to AES-128
- Results include key generation, signing, and verification operations
- Power consumption data available in CSV files
- Benchmarks executed on isolated cores ${CPU_CORES} for minimal interference

Hash Function Configuration:
- RSA: SHA-256 (matched to security level)
- ECDSA: SHA-256 (matched to curve)
- EdDSA: SHA-512 (built-in to Ed25519)
- Dilithium: SHAKE256 (post-quantum standard)
- Falcon: SHAKE256 (post-quantum standard)
- SPHINCS+-128f: SHA-256 (fast variant, for fair comparison with classical algorithms)
- SPHINCS+-128s: SHA-256 (small variant, optimized for signature size)

For detailed analysis, examine:
- CSV files for raw performance and power data
- Log files for complete benchmark output
- Power monitoring data for energy consumption patterns

Recommended Analysis:
- Energy per operation comparison at baseline security level
- Performance vs. security trade-offs at Level 1
- Classical vs. post-quantum energy efficiency
- Statistical analysis across 5 runs per algorithm
- Impact of run order randomization on results
- SPHINCS+ fast vs small variant: speed-size-energy trade-off analysis
EOF

    success "Comprehensive summary report generated: $summary_file"
}

# Cleanup function
cleanup() {
    log "Cleaning up temporary files..."
    # Keep all logs and results for analysis
    # Remove only temporary compilation artifacts if any
    rm -f *.o 2>/dev/null || true
    info "Cleanup completed (benchmark results preserved)"
}

# Verify all binaries after compilation
verify_binaries() {
    log "Verifying compiled binaries..."
    
    local binaries=("rsa_benchmark" "ecdsa_benchmark" "eddsa_benchmark" "dilithium_benchmark" "falcon_benchmark" "sphincs_benchmark")
    local available_count=0
    
    for binary in "${binaries[@]}"; do
        if [ -x "./$binary" ]; then
            success "  ✓ $binary"
            available_count=$((available_count + 1))
        else
            warning "  ✗ $binary (not available)"
        fi
    done
    
    if [ "$available_count" -eq 0 ]; then
        error "No binaries available"
        exit 1
    fi
    
    success "$available_count binaries verified and ready for benchmarking"
}

# Main execution function
main() {
    log "Starting Multi-Algorithm Cryptographic Benchmark Suite"
    log "======================================================"
    info "Target security level: NIST Level 1 (~128-bit equivalent)"
    info "Algorithms: RSA-3072, ECDSA P-256, EdDSA Ed25519, Dilithium2, Falcon-512, SPHINCS+-SHA2-128f, SPHINCS+-SHA2-128s"
    info "Execution: 5 runs per algorithm in randomized order (35 total runs)"
    info "CPU Affinity: Cores ${CPU_CORES} (isolated)"
    echo
    
    # Check prerequisites
    check_root
    
    # System preparation
    load_rapl_modules
    run_setup
    
    # Code preparation
    check_source_files
    compile_all
    verify_binaries
    
    log "All preparations complete. Starting benchmark execution..."
    echo
    
    # Run the benchmarks
    run_all_benchmarks
    
    # Generate comprehensive summary
    generate_summary
    
    success "All cryptographic benchmarks completed successfully!"
    log "Check the generated log files and CSV data for detailed results."
    info "Summary report contains analysis guidelines and file locations."
    
    cleanup
}

# Error handling
trap 'error "Script interrupted"; cleanup; exit 1' INT TERM

# Execute main function
main "$@"