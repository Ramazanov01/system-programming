#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "satellite.h"
#include "engineer.h"
#include "queue.h"

#define NUM_SATELLITES 10
#define NUM_ENGINEERS 1

pthread_mutex_t engineerMutex;
sem_t newRequest, requestHandled;

int availableEngineers = NUM_ENGINEERS;
SatelliteQueue requestQueue;

int main() {
    // Set seed for random number generation
    srand(time(NULL));
    
    // Initialize mutex and semaphores
    pthread_mutex_init(&engineerMutex, NULL);
    sem_init(&newRequest, 0, 0);
    sem_init(&requestHandled, 0, 0);

    initQueue(&requestQueue);

    pthread_t satelliteThreads[NUM_SATELLITES]; // Array for satellite threads
    pthread_t engineerThreads[NUM_ENGINEERS];    // Array for engineer threads

    // Create engineer threads
    for (int i = 0; i < NUM_ENGINEERS; ++i) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&engineerThreads[i], NULL, engineer, id);
    }

    // Create satellite threads with 1 second delay between each
    for (int i = 0; i < NUM_SATELLITES; ++i) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&satelliteThreads[i], NULL, satellite, id);
        sleep(1);  // Stagger satellite creation
    }

    // Wait for all satellite threads to complete
    for (int i = 0; i < NUM_SATELLITES; ++i) {
        pthread_join(satelliteThreads[i], NULL);
    }

    // Wait for engineer threads to complete (they will exit when queue is empty)
    for (int i = 0; i < NUM_ENGINEERS; ++i) {
        pthread_join(engineerThreads[i], NULL);
    }

    // Cleanup resources
    destroyQueue(&requestQueue);
    pthread_mutex_destroy(&engineerMutex);
    sem_destroy(&newRequest);
    sem_destroy(&requestHandled);

    return 0;
}