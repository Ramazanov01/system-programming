#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include "engineer.h"
#include "queue.h"
#include "stdlib.h"

// External declarations of shared resources
extern pthread_mutex_t engineerMutex;       // Mutex for protecting shared resources
extern sem_t newRequest, requestHandled;    // Semaphores for thread synchronization
extern int availableEngineers;              // Counter for available engineers
extern SatelliteQueue requestQueue;         // Shared priority queue of requests

void* engineer(void* arg) {
    // Get engineer ID from thread creation argument
    int id = *(int*)arg;
    free(arg);  // Free the allocated memory for the ID
    
    // Engineer's main work loop
    while (1) {
        // Wait for a new request notification (blocks until available)
        sem_wait(&newRequest);

        // --- CRITICAL SECTION START ---
        pthread_mutex_lock(&engineerMutex);
        
        // Safety check: if queue is empty, release lock and continue waiting
        if (isEmpty(&requestQueue)) {
            pthread_mutex_unlock(&engineerMutex);
            continue;
        }
        
        // Get the highest priority request from queue
        Satellite sat = dequeue(&requestQueue);
        
        // Decrement available engineers counter
        availableEngineers--;
        
        pthread_mutex_unlock(&engineerMutex);
        // --- CRITICAL SECTION END ---

        // Process the request (not critical section)
        printf("[ENGINEER %d] Handling Satellite %d (Priority %d)\n", 
               id, sat.id, sat.priority);
        
        // Simulate work processing time (2 seconds)
        sleep(2);
        
        printf("[ENGINEER %d] Finished Satellite %d\n", id, sat.id);

        // --- CRITICAL SECTION START ---
        pthread_mutex_lock(&engineerMutex);
        // Mark engineer as available again
        availableEngineers++;
        pthread_mutex_unlock(&engineerMutex);
        // --- CRITICAL SECTION END ---

        // Notify that a request has been handled
        sem_post(&requestHandled);
        
        // --- CRITICAL SECTION START ---
        pthread_mutex_lock(&engineerMutex);
        // Check if all work is done (queue is empty)
        if (isEmpty(&requestQueue)) {
            pthread_mutex_unlock(&engineerMutex);
            printf("[ENGINEER %d] Exiting...\n", id);
            break;  // Exit loop if no more work
        }
        pthread_mutex_unlock(&engineerMutex);
        // --- CRITICAL SECTION END ---
    }
    
    return NULL;  // Thread termination
}