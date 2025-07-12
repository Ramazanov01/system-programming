#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

// Global flag for handling SIGINT
volatile sig_atomic_t keep_running = 1;

// Signal handler for SIGINT
static void handle_sigint(int sig) {
    (void)sig;  // Avoid unused parameter warning
    keep_running = 0;
    printf("\nReceived SIGINT. Shutting down gracefully...\n");
}

void setup_signal_handler(void) {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

int parse_args(int argc, char *argv[], int *buffer_size, int *num_workers, 
               char **log_file, char **search_term) {
    // Check if we have the correct number of arguments
    if (argc != 5) {
        return -1;
    }

    // Parse buffer size
    *buffer_size = atoi(argv[1]);
    if (*buffer_size <= 0) {
        fprintf(stderr, "Error: Buffer size must be a positive integer\n");
        return -1;
    }

    // Parse number of workers
    *num_workers = atoi(argv[2]);
    if (*num_workers <= 0) {
        fprintf(stderr, "Error: Number of workers must be a positive integer\n");
        return -1;
    }

    // Set log file and search term
    *log_file = argv[3];
    *search_term = argv[4];

    return 0;
}

void print_usage(const char *program_name) {
    printf("Usage: %s <buffer_size> <num_workers> <log_file> <search_term>\n", program_name);
}

void *worker_thread(void *arg) {
    worker_data_t *data = (worker_data_t *)arg;
    bool done = false;
    data->match_count = 0;

    while (!done && keep_running) {
        // Get an item from the buffer
        buffer_item_t item = buffer_get(data->buffer);

        // Check if this is the EOF marker
        if (item.eof_marker) {
            // Put the EOF marker back for other workers to see
            buffer_put(data->buffer, NULL, true);
            done = true;
        } else if (item.line != NULL) {
            // Search for the term in the line
            if (strstr(item.line, data->search_term) != NULL) {
                data->match_count++;
                printf("Worker %d found match: %s", data->thread_id, item.line);
                // Add newline if not already present
                if (item.line[strlen(item.line) - 1] != '\n') {
                    printf("\n");
                }
            }
            
            // Free the line memory
            free(item.line);
        }
    }

    // Wait for all workers at the barrier
    pthread_barrier_wait(data->barrier);

    // Only the first worker that reaches the barrier will print the summary
    if (data->thread_id == 0) {
        printf("\n--- Summary Report ---\n");
    }

    // Print this worker's results
    printf("Worker %d found %d matches\n", data->thread_id, data->match_count);

    return NULL;
}

void *manager_thread(void *arg) {
    manager_data_t *data = (manager_data_t *)arg;
    FILE *file = fopen(data->filename, "r");
    
    if (file == NULL) {
        perror("Error opening log file");
        // Put EOF markers for all workers
        for (int i = 0; i < data->num_workers; i++) {
            buffer_put(data->buffer, NULL, true);
        }
        return NULL;
    }

    // Read the file line by line
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, file)) != -1 && keep_running) {
        // Put the line in the buffer
        buffer_put(data->buffer, line, false);
    }

    // Clean up
    if (line) {
        free(line);
    }
    fclose(file);

    // Add EOF marker to signal workers that all lines have been processed
    buffer_put(data->buffer, NULL, true);

    return NULL;
}