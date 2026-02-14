#ifndef BENCHMARK_FRAMEWORK_H
#define BENCHMARK_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdarg.h>

#include "power_monitoring.h"

// Common configuration constants
#define MAX_MESSAGES 1000
#define MESSAGE_SIZE 256
#define WARMUP_DURATION_SEC 240
#define BENCHMARK_DURATION_SEC 60
#define IDLE_DURATION_SEC 60
#define COOLDOWN_DURATION_SEC 240
#define MAX_GENERIC_SIGNATURE_SIZE 50000

// Common test data structures
typedef struct {
    unsigned char data[MESSAGE_SIZE];
    size_t length;
} test_message_t;

typedef struct {
    unsigned char signature[MAX_GENERIC_SIGNATURE_SIZE];
    size_t signature_length;
} test_signature_t;

// A generic context to hold all data for a benchmark run
typedef struct {
    void *algo_context; // Pointer to algorithm-specific data (e.g., keys)
    test_message_t messages[MAX_MESSAGES];
    test_signature_t signatures[MAX_MESSAGES];
    int num_messages;
} benchmark_data_t;

// Benchmark result structure - REMOVED UNCORE FIELDS
typedef struct {
    double duration_sec;
    long operations_count;
    double throughput_ops_per_sec;
    double total_energy_cpu_J;
    double total_energy_cores_J;
    double avg_power_cpu_W;
    double avg_power_cores_W;
    double energy_per_op_cpu_mJ;
    double energy_per_op_cores_mJ;
    char csv_filename[256];
} benchmark_result_t;

// Interface for cryptographic operations using function pointers
typedef struct {
    const char* name;
    const char* short_name; // e.g., "rsa", "ecdsa"

    // Initializes algo_context, generates keys and random messages.
    int (*init_and_generate_data)(benchmark_data_t *data, const char* variant);

    // Pre-computes all signatures for the verification benchmark.
    int (*precompute_signatures)(benchmark_data_t *data);

    // Performs a single signing operation.
    int (*sign_op)(benchmark_data_t *data, int msg_idx, unsigned char* temp_sig_buf, size_t* temp_sig_len);

    // Performs a single verification operation.
    int (*verify_op)(benchmark_data_t *data, int msg_idx);

    // Cleans up algorithm-specific resources (keys, etc.).
    void (*cleanup)(benchmark_data_t *data);
} crypto_operations_t;

// Main entry point for the framework
int run_complete_benchmark(const crypto_operations_t *ops, const char* variant);

// Shared logging functions
void log_printf(const char *format, ...);
int init_logging(const char* output_dir, const char* log_name_prefix);
void cleanup_logging(void);

#endif // BENCHMARK_FRAMEWORK_H