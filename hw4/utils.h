#ifndef UTILS_H
#define UTILS_H

#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include "buffer.h"


// Structure to hold worker thread data
typedef struct {
    int thread_id;                // Worker ID
    const char *search_term;      // Term to search for
    buffer_t *buffer;             // Shared buffer
    int match_count;              // Number of matches found by this worker
    pthread_barrier_t *barrier;   // Barrier for synchronization
} worker_data_t;

// Structure to hold manager thread data
typedef struct {
    const char *filename;         // Log file path
    buffer_t *buffer;             // Shared buffer
    int num_workers;              // Number of worker threads
} manager_data_t;

// Global flag for handling SIGINT
extern volatile sig_atomic_t keep_running;

// Setup signal handler for SIGINT
void setup_signal_handler(void);

// Parse command line arguments
int parse_args(int argc, char *argv[], int *buffer_size, int *num_workers, 
               char **log_file, char **search_term);

// Print usage information
void print_usage(const char *program_name);

// Worker thread function
void *worker_thread(void *arg);

// Manager thread function
void *manager_thread(void *arg);

#endif /* UTILS_H */