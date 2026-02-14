#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <sys/stat.h>
#include "benchmark_framework.h"

// Global state for the benchmark
static volatile sig_atomic_t benchmark_running = 0;
static atomic_int operations_completed = 0;
static FILE *log_file = NULL;

// --- Logging ---
void log_printf(const char *format, ...) {
    va_list args_console, args_file;
    va_start(args_console, format);
    va_start(args_file, format);
    vprintf(format, args_console);
    if (log_file) {
        vfprintf(log_file, format, args_file);
        fflush(log_file);
    }
    va_end(args_console);
    va_end(args_file);
}

int init_logging(const char* output_dir, const char* log_name_prefix) {
    char filename[512];
    char log_filename_part[256];
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    
    snprintf(log_filename_part, sizeof(log_filename_part), "benchmark_log_%s_%04d%02d%02d_%02d%02d%02d.txt",
             log_name_prefix, tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
             
    snprintf(filename, sizeof(filename), "%s/%s", output_dir, log_filename_part);

    log_file = fopen(filename, "w");
    if (!log_file) {
        fprintf(stderr, "FATAL: Could not open log file %s\n", filename);
        return -1;
    }
    log_printf("Logging output to: %s\n", filename);
    return 0;
}

void cleanup_logging(void) {
    if (log_file) {
        log_printf("Log file closed.\n");
        fclose(log_file);
        log_file = NULL;
    }
}

static int create_directory(const char *path) {
    if (mkdir(path, 0755) != 0) {
        if (errno != EEXIST) {
            log_printf("ERROR: Could not create directory %s: %s\n", path, strerror(errno));
            return -1;
        }
    } else {
        log_printf("INFO: Created output directory '%s'\n", path);
    }
    return 0;
}

// --- Benchmark Control ---
static void alarm_handler(int sig) {
    benchmark_running = 0;
}

static void cooldown_system(void) {
    log_printf("Cooling down system for %d seconds...", COOLDOWN_DURATION_SEC);
    fflush(stdout);
    double start_temp = get_current_temperature();
    sleep(COOLDOWN_DURATION_SEC);
    double end_temp = get_current_temperature();
    log_printf(" done. Temp: %.1f°C → %.1f°C\n", start_temp, end_temp);
}

static int run_warmup(const char* name, benchmark_data_t *data, const crypto_operations_t *ops, int is_signing) {
    double start_temp, end_temp;
    struct timespec start_warmup, current_time;
    long ops_count = 0;

    log_printf("Starting %s warmup phase (%d seconds)...\n", name, WARMUP_DURATION_SEC);
    start_temp = get_current_temperature();
    
    unsigned char *temp_sig_buf = NULL;
    if (is_signing) {
        temp_sig_buf = malloc(MAX_GENERIC_SIGNATURE_SIZE);
        if (!temp_sig_buf) {
            log_printf("ERROR: Failed to allocate signature buffer in warmup.\n");
            return -1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &start_warmup);
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed_sec = get_time_diff_seconds(&start_warmup, &current_time);
        if (elapsed_sec >= WARMUP_DURATION_SEC) break;

        int msg_idx = ops_count % data->num_messages;
        int result;
        if (is_signing) {
            size_t temp_sig_len = MAX_GENERIC_SIGNATURE_SIZE;
            result = ops->sign_op(data, msg_idx, temp_sig_buf, &temp_sig_len);
        } else {
            result = ops->verify_op(data, msg_idx);
        }

        if (result != 0) {
            log_printf("ERROR: Crypto operation failed during %s warmup.\n", name);
            free(temp_sig_buf);
            return -1;
        }
        ops_count++;
    }

    if(temp_sig_buf) free(temp_sig_buf);
    end_temp = get_current_temperature();
    log_printf("✓ %s warmup completed (%ld ops). Temp: %.1f°C → %.1f°C (Δ%.1f°C)\n",
               name, ops_count, start_temp, end_temp, end_temp - start_temp);
    return 0;
}

static int run_operation_benchmark(const char* output_dir, const char* name, benchmark_data_t *data, const crypto_operations_t *ops, benchmark_result_t *result, int is_signing) {
    struct timespec start_time, end_time;
    
    log_printf("Starting %s %s benchmark (%d seconds)...\n", ops->name, name, BENCHMARK_DURATION_SEC);
    if (init_monitor(10000) != 0) return -1;

    unsigned char *temp_sig_buf = NULL;
    if (is_signing) {
        temp_sig_buf = malloc(MAX_GENERIC_SIGNATURE_SIZE);
        if (!temp_sig_buf) {
            log_printf("ERROR: Failed to allocate signature buffer in benchmark.\n");
            cleanup_monitor();
            return -1;
        }
    }

    benchmark_running = 1;
    atomic_store(&operations_completed, 0);
    signal(SIGALRM, alarm_handler);

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    if (start_monitoring() != 0) {
        free(temp_sig_buf);
        cleanup_monitor();
        return -1;
    }
    alarm(BENCHMARK_DURATION_SEC);

    int msg_idx = 0;
    while (benchmark_running) {
        int op_result;
        if (is_signing) {
            size_t temp_sig_len = MAX_GENERIC_SIGNATURE_SIZE;
            op_result = ops->sign_op(data, msg_idx, temp_sig_buf, &temp_sig_len);
        } else {
            op_result = ops->verify_op(data, msg_idx);
        }
        if (op_result != 0) {
            log_printf("ERROR: Crypto operation failed during benchmark.\n");
            break;
        }
        atomic_fetch_add(&operations_completed, 1);
        msg_idx = (msg_idx + 1) % data->num_messages;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    stop_monitoring();
    if(temp_sig_buf) free(temp_sig_buf);

    power_results_t power_data;
    calculate_power_results(&power_data);

    result->duration_sec = get_time_diff_seconds(&start_time, &end_time);
    result->operations_count = atomic_load(&operations_completed);
    result->throughput_ops_per_sec = (result->duration_sec > 0) ? (result->operations_count / result->duration_sec) : 0;

    result->total_energy_cpu_J = power_data.total_energy_cpu_J;
    result->total_energy_cores_J = power_data.total_energy_cores_J;

    if (result->duration_sec > 0) {
        result->avg_power_cpu_W = result->total_energy_cpu_J / result->duration_sec;
        result->avg_power_cores_W = result->total_energy_cores_J / result->duration_sec;
    }
    if (result->operations_count > 0) {
        result->energy_per_op_cpu_mJ = (result->total_energy_cpu_J * 1000.0) / result->operations_count;
        result->energy_per_op_cores_mJ = (result->total_energy_cores_J * 1000.0) / result->operations_count;
    }

    char csv_base_name[128];
    snprintf(csv_base_name, sizeof(csv_base_name), "%s_%s_%s", ops->short_name, ops->name, name);
    char* csv_file = write_csv_file(output_dir, csv_base_name);
    if (csv_file) {
        strncpy(result->csv_filename, csv_file, sizeof(result->csv_filename) - 1);
        result->csv_filename[sizeof(result->csv_filename) - 1] = '\0';
    }

    cleanup_monitor();
    log_printf("✓ %s benchmark completed. Throughput: %.2f ops/sec\n", name, result->throughput_ops_per_sec);
    return 0;
}

static int measure_idle_baseline(const char* output_dir, benchmark_result_t *result, const crypto_operations_t *ops) {
    struct timespec start_time, end_time;

    log_printf("Measuring idle baseline energy consumption (%d seconds)...\n", IDLE_DURATION_SEC);
    if (init_monitor(10000) != 0) return -1;

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    if (start_monitoring() != 0) {
        cleanup_monitor();
        return -1;
    }
    sleep(IDLE_DURATION_SEC);
    stop_monitoring();
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    power_results_t power_data;
    calculate_power_results(&power_data);
    
    result->duration_sec = get_time_diff_seconds(&start_time, &end_time);
    result->operations_count = 0;
    result->throughput_ops_per_sec = 0;
    
    result->total_energy_cpu_J = power_data.total_energy_cpu_J;
    result->total_energy_cores_J = power_data.total_energy_cores_J;

    if (result->duration_sec > 0) {
        result->avg_power_cpu_W = result->total_energy_cpu_J / result->duration_sec;
        result->avg_power_cores_W = result->total_energy_cores_J / result->duration_sec;
    }
    
    char csv_base_name[128];
    snprintf(csv_base_name, sizeof(csv_base_name), "%s_%s_idle", ops->short_name, ops->name);
    char* csv_file = write_csv_file(output_dir, csv_base_name);
    if (csv_file) {
        strncpy(result->csv_filename, csv_file, sizeof(result->csv_filename) - 1);
        result->csv_filename[sizeof(result->csv_filename) - 1] = '\0';
    }

    cleanup_monitor();
    log_printf("✓ Idle baseline completed. Avg CPU Power: %.2f W, Avg Cores Power: %.2f W\n", 
               result->avg_power_cpu_W, result->avg_power_cores_W);
    return 0;
}

// --- Analysis and Main Flow ---
static void print_system_info(const char* algorithm_name) {
    log_printf("\n============== System Information ==============\n");
    log_printf("Algorithm:              %s\n", algorithm_name);
    log_printf("Benchmark Duration:     %d seconds\n", BENCHMARK_DURATION_SEC);
    log_printf("================================================\n\n");
}

static void analyze_results(benchmark_result_t *sign_result, benchmark_result_t *verify_result, benchmark_result_t *idle_result, const char* algorithm_name) {
    log_printf("\n======================= BENCHMARK RESULTS (%s) =======================\n", algorithm_name);

    log_printf("\nPerformance Metrics:\n");
    log_printf("   - Signing:      %ld ops in %.2fs -> %.2f ops/sec\n", sign_result->operations_count, sign_result->duration_sec, sign_result->throughput_ops_per_sec);
    log_printf("   - Verification: %ld ops in %.2fs -> %.2f ops/sec\n", verify_result->operations_count, verify_result->duration_sec, verify_result->throughput_ops_per_sec);
    if (sign_result->throughput_ops_per_sec > 0) {
        log_printf("   - Ratio (Verify/Sign): %.2fx\n", verify_result->throughput_ops_per_sec / sign_result->throughput_ops_per_sec);
    }

    log_printf("\nRaw Energy Metrics (Total):\n");
    log_printf("   - Signing (CPU):      %.2f J total, %.3f mJ/op\n", sign_result->total_energy_cpu_J, sign_result->energy_per_op_cpu_mJ);
    log_printf("   - Verification (CPU): %.2f J total, %.3f mJ/op\n", verify_result->total_energy_cpu_J, verify_result->energy_per_op_cpu_mJ);
    log_printf("   - Idle (CPU):         %.2f J total (%.2f W avg)\n", idle_result->total_energy_cpu_J, idle_result->avg_power_cpu_W);

    log_printf("\n⚡️ Dynamic Energy Analysis (CPU Package, Idle-Subtracted):\n");
    if (idle_result->duration_sec > 0) {
        double idle_power_cpu_W = idle_result->avg_power_cpu_W;
        double dynamic_energy_sign_J = sign_result->total_energy_cpu_J - (idle_power_cpu_W * sign_result->duration_sec);
        double dyn_energy_per_op_sign_mJ = (dynamic_energy_sign_J > 0 && sign_result->operations_count > 0) ? (dynamic_energy_sign_J * 1000.0) / sign_result->operations_count : 0;
        double dynamic_energy_verify_J = verify_result->total_energy_cpu_J - (idle_power_cpu_W * verify_result->duration_sec);
        double dyn_energy_per_op_verify_mJ = (dynamic_energy_verify_J > 0 && verify_result->operations_count > 0) ? (dynamic_energy_verify_J * 1000.0) / verify_result->operations_count : 0;

        log_printf("   - Signing:      %.3f mJ/op\n", dyn_energy_per_op_sign_mJ);
        log_printf("   - Verification: %.3f mJ/op\n", dyn_energy_per_op_verify_mJ);
        if (dyn_energy_per_op_verify_mJ > 0) {
            log_printf("   - Energy Ratio (Sign/Verify): %.2fx\n", dyn_energy_per_op_sign_mJ / dyn_energy_per_op_verify_mJ);
        }
        
        // Add cores energy analysis
        log_printf("\nCores Energy Metrics:\n");
        log_printf("   - Signing (Cores):      %.2f J total, %.3f mJ/op\n", sign_result->total_energy_cores_J, sign_result->energy_per_op_cores_mJ);
        log_printf("   - Verification (Cores): %.2f J total, %.3f mJ/op\n", verify_result->total_energy_cores_J, verify_result->energy_per_op_cores_mJ);
        log_printf("   - Idle (Cores):         %.2f J total (%.2f W avg)\n", idle_result->total_energy_cores_J, idle_result->avg_power_cores_W);
        
        // Add dynamic cores energy analysis
        log_printf("\n⚡️ Dynamic Energy Analysis (CPU Cores, Idle-Subtracted):\n");
        double idle_power_cores_W = idle_result->avg_power_cores_W;
        double dynamic_energy_sign_cores_J = sign_result->total_energy_cores_J - (idle_power_cores_W * sign_result->duration_sec);
        double dyn_energy_per_op_sign_cores_mJ = (dynamic_energy_sign_cores_J > 0 && sign_result->operations_count > 0) ? (dynamic_energy_sign_cores_J * 1000.0) / sign_result->operations_count : 0;
        double dynamic_energy_verify_cores_J = verify_result->total_energy_cores_J - (idle_power_cores_W * verify_result->duration_sec);
        double dyn_energy_per_op_verify_cores_mJ = (dynamic_energy_verify_cores_J > 0 && verify_result->operations_count > 0) ? (dynamic_energy_verify_cores_J * 1000.0) / verify_result->operations_count : 0;

        log_printf("   - Signing:      %.3f mJ/op\n", dyn_energy_per_op_sign_cores_mJ);
        log_printf("   - Verification: %.3f mJ/op\n", dyn_energy_per_op_verify_cores_mJ);
        if (dyn_energy_per_op_verify_cores_mJ > 0) {
            log_printf("   - Energy Ratio (Sign/Verify): %.2fx\n", dyn_energy_per_op_sign_cores_mJ / dyn_energy_per_op_verify_cores_mJ);
        }
    } else {
        log_printf("   - Could not perform dynamic analysis: idle measurement invalid.\n");
    }

    log_printf("\nOutput Files:\n");
    log_printf("   - Signing:      %s\n", sign_result->csv_filename);
    log_printf("   - Verification: %s\n", verify_result->csv_filename);
    log_printf("   - Idle:         %s\n", idle_result->csv_filename);
    log_printf("====================================================================================\n");
}

int run_complete_benchmark(const crypto_operations_t *ops, const char* variant) {
    const char* output_dir = ops->short_name;

    if (create_directory(output_dir) != 0) {
        return 1;
    }

    if (init_logging(output_dir, ops->short_name) != 0) return 1;
    
    benchmark_data_t *test_data = malloc(sizeof(benchmark_data_t));
    if (test_data == NULL) {
        log_printf("FATAL: Failed to allocate memory for benchmark_data_t\n");
        cleanup_logging();
        return 1;
    }
    memset(test_data, 0, sizeof(benchmark_data_t));

    benchmark_result_t sign_result = {0};
    benchmark_result_t verify_result = {0};
    benchmark_result_t idle_result = {0};
    int ret_code = 0;
    
    char full_algo_name[128];
    snprintf(full_algo_name, sizeof(full_algo_name), "%s-%s", ops->short_name, variant);

    log_printf("🚀 Starting Power Consumption Benchmark for %s 🚀\n", full_algo_name);
    print_system_info(full_algo_name);

    log_printf("Validating RAPL domains...\n");
    if (init_monitor(1000) != 0 || validate_rapl_domains() != 0) {
        log_printf("RAPL initialization or validation failed. Exiting.\n");
        cleanup_monitor();
        ret_code = 1;
        goto cleanup;
    }
    cleanup_monitor();
    log_printf("✓ RAPL domains validated.\n");

    log_printf("\n--- ⚙️ Prepare Test Data ---\n");
    if (ops->init_and_generate_data(test_data, variant) != 0) {
        ret_code = 1;
        goto cleanup;
    }
    if (ops->precompute_signatures(test_data) != 0) {
        ret_code = 1;
        goto cleanup;
    }

    // --- SIGNING BENCHMARK ---
    log_printf("\n--- ✏️ Signing Benchmark Run ---\n");
    cooldown_system();
    if (run_warmup("signing", test_data, ops, 1) != 0 || 
        run_operation_benchmark(output_dir, "signing", test_data, ops, &sign_result, 1) != 0) {
        log_printf("Signing benchmark run failed.\n");
        ret_code = 1;
        goto cleanup;
    }

    // --- VERIFICATION BENCHMARK ---
    log_printf("\n--- ✔️ Verification Benchmark Run ---\n");
    cooldown_system();
    if (run_warmup("verification", test_data, ops, 0) != 0 ||
        run_operation_benchmark(output_dir, "verification", test_data, ops, &verify_result, 0) != 0) {
        log_printf("Verification benchmark run failed.\n");
        ret_code = 1;
        goto cleanup;
    }

    // --- IDLE BASELINE ---
    log_printf("\n--- 💤 Measure Idle Baseline ---\n");
    cooldown_system();
    if (measure_idle_baseline(output_dir, &idle_result, ops) != 0) {
        log_printf("Idle baseline measurement failed.\n");
        ret_code = 1;
        goto cleanup;
    }

    // --- ANALYSIS ---
    log_printf("\n--- 📊 Data Analysis ---\n");
    analyze_results(&sign_result, &verify_result, &idle_result, full_algo_name);

cleanup:
    log_printf("\nCleaning up resources...\n");
    ops->cleanup(test_data);
    free(test_data);
    log_printf("\n✅ Benchmark Complete.\n");
    cleanup_logging();
    return ret_code;
}