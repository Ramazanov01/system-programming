#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "buffer.h"
#include "utils.h"

int main(int argc, char *argv[]) {
    int buffer_size, num_workers;
    char *log_file, *search_term;
    
    // Parse command line arguments
    if (parse_args(argc, argv, &buffer_size, &num_workers, &log_file, &search_term) != 0) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    // Set up signal handler for SIGINT
    setup_signal_handler();
    
    // Initialize the buffer
    buffer_t buffer;
    if (buffer_init(&buffer, buffer_size) != 0) {
        fprintf(stderr, "Failed to initialize buffer\n");
        return EXIT_FAILURE;
    }
    
    // Set up barrier for worker synchronization
    pthread_barrier_t barrier;
    if (pthread_barrier_init(&barrier, NULL, num_workers) != 0) {
        perror("Failed to initialize barrier");
        buffer_destroy(&buffer);
        return EXIT_FAILURE;
    }
    
    // Create worker thread data
    worker_data_t *worker_data = malloc(num_workers * sizeof(worker_data_t));
    if (!worker_data) {
        perror("Failed to allocate worker data");
        pthread_barrier_destroy(&barrier);
        buffer_destroy(&buffer);
        return EXIT_FAILURE;
    }
    
    // Initialize worker thread data
    for (int i = 0; i < num_workers; i++) {
        worker_data[i].thread_id = i;
        worker_data[i].search_term = search_term;
        worker_data[i].buffer = &buffer;
        worker_data[i].barrier = &barrier;
        worker_data[i].match_count = 0;
    }
    
    // Create manager thread data
    manager_data_t manager_data;
    manager_data.filename = log_file;
    manager_data.buffer = &buffer;
    manager_data.num_workers = num_workers;
    
    // Create worker threads
    pthread_t *worker_threads = malloc(num_workers * sizeof(pthread_t));
    if (!worker_threads) {
        perror("Failed to allocate thread handles");
        free(worker_data);
        pthread_barrier_destroy(&barrier);
        buffer_destroy(&buffer);
        return EXIT_FAILURE;
    }
    
    // Start worker threads
    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker_thread, &worker_data[i]) != 0) {
            perror("Failed to create worker thread");
            // Clean up already created threads
            for (int j = 0; j < i; j++) {
                pthread_cancel(worker_threads[j]);
                pthread_join(worker_threads[j], NULL);
            }
            free(worker_threads);
            free(worker_data);
            pthread_barrier_destroy(&barrier);
            buffer_destroy(&buffer);
            return EXIT_FAILURE;
        }
    }
    
    // Create manager thread
    pthread_t manager_thread_id;
    if (pthread_create(&manager_thread_id, NULL, manager_thread, &manager_data) != 0) {
        perror("Failed to create manager thread");
        // Clean up worker threads
        for (int i = 0; i < num_workers; i++) {
            pthread_cancel(worker_threads[i]);
            pthread_join(worker_threads[i], NULL);
        }
        free(worker_threads);
        free(worker_data);
        pthread_barrier_destroy(&barrier);
        buffer_destroy(&buffer);
        return EXIT_FAILURE;
    }
    
    // Wait for manager thread to complete
    pthread_join(manager_thread_id, NULL);
    
    // Wait for all worker threads to complete
    for (int i = 0; i < num_workers; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    
    // Calculate and print total matches
    int total_matches = 0;
    for (int i = 0; i < num_workers; i++) {
        total_matches += worker_data[i].match_count;
    }
    printf("\nTotal matches found: %d\n", total_matches);
    
    // Clean up
    free(worker_threads);
    free(worker_data);
    pthread_barrier_destroy(&barrier);
    buffer_destroy(&buffer);
    
    return EXIT_SUCCESS;
}