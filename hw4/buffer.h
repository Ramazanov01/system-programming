#ifndef BUFFER_H
#define BUFFER_H

#include <pthread.h>
#include <stdbool.h>

// Structure to hold a line from the log file
typedef struct {
    char *line;     // The content of the line
    bool eof_marker; // Flag to indicate end of file
} buffer_item_t;

// Circular buffer structure
typedef struct {
    buffer_item_t *items;    // Array of buffer items
    int capacity;            // Maximum buffer size
    int count;               // Current number of items
    int in;                  // Index for adding items (producer)
    int out;                 // Index for removing items (consumer)
    pthread_mutex_t mutex;   // Mutex for synchronization
    pthread_cond_t not_full; // Condition variable for not full buffer
    pthread_cond_t not_empty; // Condition variable for not empty buffer
} buffer_t;

// Initialize the buffer with given capacity
int buffer_init(buffer_t *buffer, int capacity);

// Clean up buffer resources
void buffer_destroy(buffer_t *buffer);

// Add an item to the buffer (producer)
int buffer_put(buffer_t *buffer, const char *line, bool eof_marker);

// Get an item from the buffer (consumer)
buffer_item_t buffer_get(buffer_t *buffer);

#endif /* BUFFER_H */