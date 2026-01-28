#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sched.h>
#include <stdatomic.h>
#include <linux/limits.h>
#include "power_monitoring.h"

// Global monitor instance
rapl_monitor_t monitor = {0};

// Static arrays and constants
const char* domain_names[] = {"cpu_package", "cores"};
static const char* rapl_paths[] = {
    "/sys/class/powercap/intel-rapl/intel-rapl:0",                    // CPU Package
    "/sys/class/powercap/intel-rapl/intel-rapl:0/intel-rapl:0:0"      // CPU Cores  
};

double get_time_diff_seconds(struct timespec *start, struct timespec *end) {
    double start_sec = start->tv_sec + start->tv_nsec / 1000000000.0;
    double end_sec = end->tv_sec + end->tv_nsec / 1000000000.0;
    return end_sec - start_sec;
}

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

static inline void precise_sleep_us(uint32_t microseconds) {
    struct timespec req;
    req.tv_sec = microseconds / 1000000;
    req.tv_nsec = (microseconds % 1000000) * 1000;
    
    while (nanosleep(&req, &req) == -1 && errno == EINTR);
}

// Hybrid approach: try to read from open FD, reopen if error
static inline int read_uint64_hybrid(rapl_domain_t *domain, uint64_t *value) {
    char buffer[32];
    ssize_t bytes_read;
    int attempts = 0;
    
    while (attempts < 2) {
        if (domain->energy_fd >= 0) {
            if (lseek(domain->energy_fd, 0, SEEK_SET) == 0) {
                bytes_read = read(domain->energy_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    *value = strtoull(buffer, NULL, 10);
                    return 0;
                }
            }
            
            close(domain->energy_fd);
            domain->energy_fd = -1;
        }
        
        domain->energy_fd = open(domain->energy_path, O_RDONLY);
        if (domain->energy_fd < 0) {
            return -1;
        }
        
        attempts++;
    }
    
    return -1;
}

// Hybrid approach for temperature reading
static inline int read_temperature_hybrid(double *temperature) {
    char buffer[32];
    ssize_t bytes_read;
    int attempts = 0;
    
    while (attempts < 2) {
        if (monitor.temp_fd >= 0) {
            if (lseek(monitor.temp_fd, 0, SEEK_SET) == 0) {
                bytes_read = read(monitor.temp_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    *temperature = strtod(buffer, NULL);
                    return 0;
                }
            }
            
            close(monitor.temp_fd);
            monitor.temp_fd = -1;
        }
        
        monitor.temp_fd = open(monitor.temp_path, O_RDONLY);
        if (monitor.temp_fd < 0) {
            return -1;
        }
        
        attempts++;
    }
    
    return -1;
}

static int init_rapl_domain(rapl_domain_t *domain, int domain_idx) {
    struct stat st;
    char max_energy_path[MAX_PATH_LEN];
    uint64_t max_energy;
    
    // Build paths
    snprintf(domain->energy_path, MAX_PATH_LEN, "%s/energy_uj", rapl_paths[domain_idx]);
    snprintf(max_energy_path, MAX_PATH_LEN, "%s/max_energy_range_uj", rapl_paths[domain_idx]);
    
    // Check if domain exists
    if (stat(domain->energy_path, &st) != 0) {
        domain->available = 0;
        domain->energy_fd = -1;
        return 0; // Not an error, just not available
    }
    
    // Try to open file descriptor (will be reopened if needed)
    domain->energy_fd = open(domain->energy_path, O_RDONLY);
    
    if (domain->energy_fd < 0) {
        printf("Warning: Cannot open energy file for %s, will try reopening during monitoring\n", domain_names[domain_idx]);
    }
    
    // Get max energy range for wraparound detection
    int max_fd = open(max_energy_path, O_RDONLY);
    if (max_fd >= 0) {
        char buffer[32];
        ssize_t bytes_read = read(max_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            max_energy = strtoull(buffer, NULL, 10);
            domain->max_energy_range = max_energy;
        } else {
            domain->max_energy_range = UINT64_MAX;
        }
        close(max_fd);
    } else {
        domain->max_energy_range = UINT64_MAX;
    }
    
    // Initialize last reading using hybrid approach
    uint64_t initial_energy;
    if (read_uint64_hybrid(domain, &initial_energy) == 0) {
        domain->last_energy_raw = initial_energy;
        domain->wraparound_count = 0;
        domain->available = 1;
        printf("Initialized RAPL domain %s successfully\n", domain_names[domain_idx]);
    } else {
        printf("Warning: Failed to get initial reading for %s\n", domain_names[domain_idx]);
        domain->available = 0;
        domain->energy_fd = -1;
    }
    
    return 0;
}

static int init_temperature_sensor(void) {
    char path[MAX_PATH_LEN];
    char type_buffer[64];
    int type_fd;
    int max_thermal_zones = 30;
    
    // Try to determine actual number of thermal zones by checking directory
    DIR *thermal_dir = opendir("/sys/class/thermal");
    if (thermal_dir) {
        struct dirent *entry;
        int found_zones = 0;
        while ((entry = readdir(thermal_dir)) != NULL) {
            if (strncmp(entry->d_name, "thermal_zone", 12) == 0) {
                int zone_num = atoi(entry->d_name + 12);
                if (zone_num >= found_zones) {
                    found_zones = zone_num + 1;
                }
            }
        }
        closedir(thermal_dir);
        
        if (found_zones > 0) {
            max_thermal_zones = found_zones;
            printf("Found %d thermal zones (0-%d)\n", found_zones, found_zones - 1);
        }
    } else {
        printf("Cannot read /sys/class/thermal directory, using default range 0-29\n");
    }
    
    // First priority: search specifically for x86_pkg_temp
    for (int i = 0; i < max_thermal_zones; i++) {
        snprintf(path, MAX_PATH_LEN, "/sys/class/thermal/thermal_zone%d/type", i);
        type_fd = open(path, O_RDONLY);
        if (type_fd < 0) continue;
        
        ssize_t bytes_read = read(type_fd, type_buffer, sizeof(type_buffer) - 1);
        close(type_fd);
        
        if (bytes_read > 0) {
            type_buffer[bytes_read] = '\0';
            char *newline = strchr(type_buffer, '\n');
            if (newline) *newline = '\0';
            
            if (strcmp(type_buffer, "x86_pkg_temp") == 0) {
                snprintf(monitor.temp_path, MAX_PATH_LEN, "/sys/class/thermal/thermal_zone%d/temp", i);
                monitor.temp_fd = open(monitor.temp_path, O_RDONLY);
                if (monitor.temp_fd >= 0) {
                    printf("Found preferred temperature sensor: x86_pkg_temp at thermal_zone%d\n", i);
                    return 0;
                }
            }
        }
    }
    
    // If x86_pkg_temp not found, search for fallback sensor types
    const char* fallback_temp_types[] = {"coretemp", "cpu_thermal", "Package", "CPU"};
    int num_fallback_types = sizeof(fallback_temp_types) / sizeof(fallback_temp_types[0]);
    
    printf("x86_pkg_temp not found, searching for fallback temperature sensors...\n");
    
    for (int i = 0; i < max_thermal_zones; i++) {
        snprintf(path, MAX_PATH_LEN, "/sys/class/thermal/thermal_zone%d/type", i);
        type_fd = open(path, O_RDONLY);
        if (type_fd < 0) continue;
        
        ssize_t bytes_read = read(type_fd, type_buffer, sizeof(type_buffer) - 1);
        close(type_fd);
        
        if (bytes_read > 0) {
            type_buffer[bytes_read] = '\0';
            char *newline = strchr(type_buffer, '\n');
            if (newline) *newline = '\0';
            
            for (int j = 0; j < num_fallback_types; j++) {
                if (strstr(type_buffer, fallback_temp_types[j])) {
                    snprintf(monitor.temp_path, MAX_PATH_LEN, "/sys/class/thermal/thermal_zone%d/temp", i);
                    monitor.temp_fd = open(monitor.temp_path, O_RDONLY);
                    if (monitor.temp_fd >= 0) {
                        printf("Found fallback temperature sensor: %s at thermal_zone%d\n", type_buffer, i);
                        return 0;
                    }
                }
            }
        }
    }
    
    printf("No compatible temperature sensor found\n");
    monitor.temp_fd = -1;
    return -1;
}

static inline double handle_wraparound(rapl_domain_t *domain, uint64_t current_raw) {
    uint64_t energy_diff;
    
    if (current_raw < domain->last_energy_raw) {
        // Wraparound detected
        domain->wraparound_count++;
        energy_diff = (domain->max_energy_range - domain->last_energy_raw) + current_raw;
    } else {
        energy_diff = current_raw - domain->last_energy_raw;
    }
    
    domain->last_energy_raw = current_raw;
    return (double)energy_diff / 1000000.0; // Convert microjoules to joules
}

// High-speed monitoring thread with CPU affinity
static void* monitor_thread_func(void *arg) {
    uint64_t target_time, current_time, sleep_time_us;
    rapl_reading_t *reading;
    uint64_t raw_value;
    double temp_value;
    int i;
    
    target_time = get_time_ns();
    
    while (monitor.monitoring && monitor.reading_count < MAX_READINGS) {
        current_time = get_time_ns();
        reading = &monitor.readings[monitor.reading_count];
        
        // Record timestamp first for maximum accuracy
        reading->timestamp_ns = current_time;
        reading->interval_us = monitor.interval_us;
        
        // Read energy values and handle wraparound using hybrid approach
        for (i = 0; i < 2; i++) {
            if (monitor.domains[i].available) {
                if (read_uint64_hybrid(&monitor.domains[i], &raw_value) == 0) {
                    double energy_diff = handle_wraparound(&monitor.domains[i], raw_value);
                    
                    switch (i) {
                        case 0: 
                            reading->cpu_package_energy = energy_diff;
                            break;
                        case 1: 
                            reading->cores_energy = energy_diff;
                            break;
                    }
                } else {
                    printf("Warning: Failed to read energy for domain %s at sample %d\n", 
                           domain_names[i], monitor.reading_count);
                }
            }
        }
        
        // Read temperature using hybrid approach
        if (read_temperature_hybrid(&temp_value) == 0) {
            reading->temperature = temp_value / 1000.0; // Convert millicelsius to celsius
        }
        
        monitor.reading_count++;
        
        // Add debug output every 1000 samples
        if (monitor.reading_count % 1000 == 0) {
            printf("Monitor: %d readings collected\n", monitor.reading_count);
        }
        
        // Precise timing control
        target_time += monitor.interval_us * 1000; // Convert to nanoseconds
        current_time = get_time_ns();
        
        if (target_time > current_time) {
            sleep_time_us = (target_time - current_time) / 1000;
            if (sleep_time_us > 0) {
                precise_sleep_us(sleep_time_us);
            }
        }
    }
    
    printf("Monitor thread exiting with %d readings\n", monitor.reading_count);
    return NULL;
}

// Initialize monitoring system
int init_monitor(uint32_t interval_us) {
    int i;
    
    // Allocate readings buffer
    monitor.readings = malloc(MAX_READINGS * sizeof(rapl_reading_t));
    if (!monitor.readings) {
        fprintf(stderr, "Failed to allocate memory for readings\n");
        return -1;
    }
    
    memset(monitor.readings, 0, MAX_READINGS * sizeof(rapl_reading_t));
    monitor.interval_us = interval_us;
    monitor.reading_count = 0;
    
    // Initialize RAPL domains
    for (i = 0; i < 2; i++) {
        if (init_rapl_domain(&monitor.domains[i], i) != 0) {
            fprintf(stderr, "Warning: Failed to initialize RAPL domain %s\n", domain_names[i]);
        }
    }
    
    // Initialize temperature sensor
    if (init_temperature_sensor() != 0) {
        fprintf(stderr, "Warning: Temperature sensor not available\n");
    }
    
    return 0;
}

// Start monitoring
int start_monitoring(void) {
    if (monitor.monitoring) {
        fprintf(stderr, "Warning: Monitoring already active\n");
        return -1;
    }
    monitor.monitoring = 1;
    monitor.reading_count = 0;
    clock_gettime(CLOCK_MONOTONIC_RAW, &monitor.start_time);

    printf("Starting monitoring thread (interval: %u us)...\n", monitor.interval_us);

    struct sched_param param_monitor;
    param_monitor.sched_priority = 98; // High priority

    int s = pthread_create(&monitor.monitor_thread, NULL, monitor_thread_func, NULL);
    if (s != 0) {
        perror("pthread_create for monitor");
        monitor.monitoring = 0;
        return -1;
    }

    s = pthread_setschedparam(monitor.monitor_thread, SCHED_FIFO, &param_monitor);
    if (s != 0) {
        fprintf(stderr, "WARNING: Could not set real-time priority for monitoring thread: %s\n", strerror(s));
    } else {
        printf("Monitoring thread set to SCHED_FIFO priority %d.\n", param_monitor.sched_priority);
    }

    printf("Monitoring thread started\n");
    return 0;
}

// Stop monitoring
void stop_monitoring(void) {
    if (!monitor.monitoring) {
        printf("Monitoring was not active\n");
        return;
    }
    
    printf("Stopping monitoring thread...\n");
    monitor.monitoring = 0;
    
    // Wait for thread to finish with timeout
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5; // 5 second timeout
    
    int result = pthread_timedjoin_np(monitor.monitor_thread, NULL, &timeout);
    if (result == ETIMEDOUT) {
        fprintf(stderr, "Warning: Monitor thread did not exit within timeout\n");
        pthread_cancel(monitor.monitor_thread);
        pthread_join(monitor.monitor_thread, NULL);
    } else if (result != 0) {
        fprintf(stderr, "Error joining monitor thread: %d\n", result);
    }
    
    printf("Monitoring stopped. Collected %d readings\n", monitor.reading_count);
}

static void generate_csv_filename(const char *algorithm, char *filename) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    
    snprintf(filename, MAX_FILENAME_LEN, "%s_%04d%02d%02d_%02d%02d%02d.csv",
             algorithm,
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec);
}

char* write_csv_file(const char *output_dir, const char *algorithm) {
    static char full_filepath[MAX_FILENAME_LEN];
    char filename_part[MAX_FILENAME_LEN];
    FILE *csv_file;
    double total_energy[2] = {0};
    double min_temp = 1000, max_temp = -1000, avg_temp = 0;
    int temp_readings = 0;
    double duration_sec;
    int effective_readings = monitor.reading_count - 1;
    
    if (monitor.reading_count <= 1) {
        printf("Insufficient readings collected (need at least 2)\n");
        return NULL;
    }
    
    generate_csv_filename(algorithm, filename_part);
    snprintf(full_filepath, MAX_FILENAME_LEN, "%s/%s", output_dir, filename_part);

    csv_file = fopen(full_filepath, "w");
    if (!csv_file) {
        fprintf(stderr, "Failed to create CSV file: %s\n", full_filepath);
        return NULL;
    }
    printf("Creating CSV file: %s\n", full_filepath);
    
    // Calculate totals from readings (excluding first baseline reading)
    for (int i = 1; i < monitor.reading_count; i++) {
        rapl_reading_t *r = &monitor.readings[i];
        total_energy[0] += r->cpu_package_energy;
        total_energy[1] += r->cores_energy;
        
        if (r->temperature > 0) {
            if (r->temperature < min_temp) min_temp = r->temperature;
            if (r->temperature > max_temp) max_temp = r->temperature;
            avg_temp += r->temperature;
            temp_readings++;
        }
    }
    
    duration_sec = (effective_readings * monitor.interval_us) / 1000000.0;
    if (temp_readings > 0) avg_temp /= temp_readings;
    
    // Write CSV header - ENERGY ONLY
    fprintf(csv_file, "sample,timestamp_ns,");
    for (int i = 0; i < 2; i++) {
        if (monitor.domains[i].available) {
            fprintf(csv_file, "%s_energy_J,", domain_names[i]);
        }
    }
    if (monitor.temp_fd >= 0) fprintf(csv_file, "temperature_C,");
    fprintf(csv_file, "interval_us\n");
    
    // Write data rows - ENERGY ONLY
    for (int i = 1; i < monitor.reading_count; i++) {
        rapl_reading_t *r = &monitor.readings[i];
        fprintf(csv_file, "%d,%lu,", i, r->timestamp_ns);
        if (monitor.domains[0].available) fprintf(csv_file, "%.6f,", r->cpu_package_energy);
        if (monitor.domains[1].available) fprintf(csv_file, "%.6f,", r->cores_energy);
        if (monitor.temp_fd >= 0) fprintf(csv_file, "%.1f,", r->temperature);
        fprintf(csv_file, "%u\n", r->interval_us);
    }
    
    // Write totals row - ENERGY ONLY
    fprintf(csv_file, "TOTAL,,");
    for (int i = 0; i < 2; i++) {
        if (monitor.domains[i].available) fprintf(csv_file, "%.6f,", total_energy[i]);
    }
    if (monitor.temp_fd >= 0) fprintf(csv_file, "%.1f,", avg_temp);
    fprintf(csv_file, "%u\n", monitor.interval_us);
    
    fclose(csv_file);
    printf("CSV file created successfully: %s\n", full_filepath);
    printf("Total effective readings: %d (first reading excluded)\n", effective_readings);
    printf("Duration: %.3f seconds\n", duration_sec);
    printf("Total CPU Package Energy: %.6f J\n", total_energy[0]);
    printf("Total Cores Energy: %.6f J\n", total_energy[1]);
    
    return full_filepath;
}

// Calculate power results - ENERGY ONLY
void calculate_power_results(power_results_t *results) {
    if (!results) return;

    results->total_energy_cpu_J = 0;
    results->total_energy_cores_J = 0;

    if (monitor.reading_count <= 1) {
        printf("Insufficient readings to calculate power results.\n");
        return;
    }

    // Sum energy from all readings, excluding the first one which is a baseline
    for (int i = 1; i < monitor.reading_count; i++) {
        results->total_energy_cpu_J += monitor.readings[i].cpu_package_energy;
        results->total_energy_cores_J += monitor.readings[i].cores_energy;
    }
}

int validate_rapl_domains(void) {
    int available_domains = 0;
    int critical_domains = 0;
    
    printf("Validating RAPL domains:\n");
    
    for (int i = 0; i < 2; i++) {
        if (monitor.domains[i].available) {
            available_domains++;
            printf("  ✓ %s: Available (energy_fd=%d)\n", 
                   domain_names[i], monitor.domains[i].energy_fd);
            
            // Both CPU package and cores are critical for measurement
            critical_domains++;
        } else {
            printf("  ✗ %s: Not available\n", domain_names[i]);
        }
    }
    
    printf("Available domains: %d/2\n", available_domains);
    
    // We need at least CPU package or cores domain for meaningful measurement
    if (critical_domains == 0) {
        fprintf(stderr, "ERROR: No critical RAPL domains (CPU package/cores) available!\n");
        fprintf(stderr, "This system may not support RAPL or you may need root privileges.\n");
        return -1;
    }
    
    if (available_domains == 0) {
        fprintf(stderr, "ERROR: No RAPL domains available!\n");
        fprintf(stderr, "Possible causes:\n");
        fprintf(stderr, "  - Not running as root\n");
        fprintf(stderr, "  - RAPL not supported on this CPU\n");
        fprintf(stderr, "  - intel_rapl module not loaded\n");
        return -1;
    }
    
    printf("RAPL validation passed (%d critical domains available)\n", critical_domains);
    return 0;
}

double get_current_temperature(void) {
    double temp_raw;

    // Initialize the temperature sensor if it hasn't been already.
    if (monitor.temp_fd < 0) {
        if (init_temperature_sensor() != 0) {
            static int warning_printed = 0;
            if (!warning_printed) {
                fprintf(stderr, "Warning: Could not initialize temperature sensor. Returning placeholder.\n");
                warning_printed = 1;
            }
            return 45.0; // Return a placeholder value
        }
    }

    // Read the temperature using the robust hybrid-read function.
    if (read_temperature_hybrid(&temp_raw) == 0) {
        return temp_raw / 1000.0; // Convert from millidegrees to degrees Celsius
    }

    // If reading fails, return the placeholder.
    return 45.0;
}

void cleanup_monitor(void) {
    if (monitor.monitoring) {
        stop_monitoring();
    }
    
    for (int i = 0; i < 2; i++) {
        if (monitor.domains[i].energy_fd >= 0) {
            close(monitor.domains[i].energy_fd);
            monitor.domains[i].energy_fd = -1;
        }
    }
    
    if (monitor.temp_fd >= 0) {
        close(monitor.temp_fd);
        monitor.temp_fd = -1;
    }
    
    if (monitor.readings) {
        free(monitor.readings);
        monitor.readings = NULL;
    }
}