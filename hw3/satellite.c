#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include "satellite.h"
#include "queue.h"

// External declarations of shared resources
extern pthread_mutex_t engineerMutex;       // Mutex for protecting shared resources
extern sem_t newRequest, requestHandled;    // Semaphores for thread synchronization
extern int availableEngineers;              // Counter for available engineers
extern SatelliteQueue requestQueue;         // Shared priority queue of requests

void* satellite(void* arg) {
    // Get satellite ID from thread creation argument
    int id = *(int*)arg;
    free(arg);  // Free the allocated memory for the ID
    
    // Generate random priority (1-5) for this request
    srand(time(NULL) + id);  // Seed random with time + unique ID
    int priority = rand() % 5 + 1;
    
    // Generate random timeout period (1-6 seconds)
    int waitTime = rand() % 6 + 1;

    // Create satellite request structure
    Satellite sat = { id, priority };

    // Print request information
    printf("[SATELLITE] Satellite %d requesting (priority %d)\n", id, priority);

    // --- CRITICAL SECTION START ---
    pthread_mutex_lock(&engineerMutex);
    // Add request to the shared priority queue
    enqueue(&requestQueue, sat);
    pthread_mutex_unlock(&engineerMutex);
    // --- CRITICAL SECTION END ---

    // Signal engineers that a new request is available
    sem_post(&newRequest);

    // Prepare timeout structure
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);  // Get current time
    ts.tv_sec += waitTime;               // Set timeout time

    // Wait for request to be handled, with timeout
    if (sem_timedwait(&requestHandled, &ts) == -1) {
        // Handle timeout case
        if (errno == ETIMEDOUT) {
            // --- CRITICAL SECTION START ---
            pthread_mutex_lock(&engineerMutex);
            // Check if request is still in queue
            int found = 0;
            for (int i = 0; i < requestQueue.size; ++i) {
                if (requestQueue.data[i].id == id) {
                    found = 1;
                    // Remove the timed-out request
                    removeSatellite(&requestQueue, id);
                    break;
                }
            }
            pthread_mutex_unlock(&engineerMutex);
            // --- CRITICAL SECTION END ---
            
            // Print timeout message if request was still pending
            if (found) {
                printf("[TIMEOUT] Satellite %d timeout %d second.\n", id, waitTime);
            }
        }
    }

    return NULL;  // Thread termination
}