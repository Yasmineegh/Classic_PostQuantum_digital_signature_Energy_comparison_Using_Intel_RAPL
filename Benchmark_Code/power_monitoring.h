#ifndef POWER_MONITORING_H
#define POWER_MONITORING_H

#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>

#define MAX_READINGS 1000000
#define MAX_PATH_LEN 256
#define NSEC_PER_SEC 1000000000UL
#define NSEC_PER_MSEC 1000000UL
#define MAX_FILENAME_LEN 512

// RAPL reading structure - ENERGY ONLY (no power in Watts)
typedef struct {
    uint64_t timestamp_ns;
    double cpu_package_energy;  // Joules
    double cores_energy;         // Joules
    double temperature;          // Celsius
    uint32_t interval_us;        // Microseconds
} rapl_reading_t;

typedef struct {
    char energy_path[MAX_PATH_LEN];
    int energy_fd;
    uint64_t max_energy_range;
    uint64_t last_energy_raw;
    uint64_t wraparound_count;
    int available;
} rapl_domain_t;

typedef struct {
    rapl_domain_t domains[2];  // CPU package and cores only
    int temp_fd;
    char temp_path[MAX_PATH_LEN];
    rapl_reading_t *readings;
    _Atomic int monitoring;
    _Atomic int reading_count;
    uint32_t interval_us;
    pthread_t monitor_thread;
    struct timespec start_time;
} rapl_monitor_t;

// Results struct - ENERGY ONLY (no power calculations)
typedef struct {
    double total_energy_cpu_J;    // Total CPU package energy in Joules
    double total_energy_cores_J;  // Total cores energy in Joules
} power_results_t;

// Global monitor instance
extern rapl_monitor_t monitor;

// Function declarations
double get_time_diff_seconds(struct timespec *start, struct timespec *end);
int init_monitor(uint32_t interval_us);
int start_monitoring(void);
void stop_monitoring(void);
int validate_rapl_domains(void);
char* write_csv_file(const char *output_dir, const char *algorithm);
void cleanup_monitor(void);
void calculate_power_results(power_results_t *results);
double get_current_temperature(void);

#endif // POWER_MONITORING_H