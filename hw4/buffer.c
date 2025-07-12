#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int buffer_init(buffer_t *buffer, int capacity) {
    buffer->items = (buffer_item_t *)malloc(capacity * sizeof(buffer_item_t));
    if (!buffer->items) {
        perror("Failed to allocate buffer memory");
        return -1;
    }

    buffer->capacity = capacity;
    buffer->count = 0;
    buffer->in = 0;
    buffer->out = 0;

    // Initialize synchronization primitives
    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        perror("Failed to initialize mutex");
        free(buffer->items);
        return -1;
    }

    if (pthread_cond_init(&buffer->not_full, NULL) != 0) {
        perror("Failed to initialize not_full condition variable");
        pthread_mutex_destroy(&buffer->mutex);
        free(buffer->items);
        return -1;
    }

    if (pthread_cond_init(&buffer->not_empty, NULL) != 0) {
        perror("Failed to initialize not_empty condition variable");
        pthread_cond_destroy(&buffer->not_full);
        pthread_mutex_destroy(&buffer->mutex);
        free(buffer->items);
        return -1;
    }

    return 0;
}

void buffer_destroy(buffer_t *buffer) {
    // Free all strings in the buffer
    for (int i = 0; i < buffer->capacity; i++) {
        if (buffer->items[i].line != NULL) {
            free(buffer->items[i].line);
            buffer->items[i].line = NULL;
        }
    }

    // Free the buffer array
    free(buffer->items);
    buffer->items = NULL;

    // Destroy synchronization primitives
    pthread_mutex_destroy(&buffer->mutex);
    pthread_cond_destroy(&buffer->not_full);
    pthread_cond_destroy(&buffer->not_empty);
}

int buffer_put(buffer_t *buffer, const char *line, bool eof_marker) {
    pthread_mutex_lock(&buffer->mutex);

    // Wait until there's space in the buffer
    while (buffer->count == buffer->capacity) {
        pthread_cond_wait(&buffer->not_full, &buffer->mutex);
    }

    // Create a new buffer item
    buffer_item_t item;
    item.eof_marker = eof_marker;

    if (!eof_marker && line != NULL) {
        // Allocate memory for the line and copy it
        item.line = strdup(line);
        if (item.line == NULL) {
            pthread_mutex_unlock(&buffer->mutex);
            return -1;
        }
    } else {
        item.line = NULL;  // EOF marker doesn't need a line
    }

    // Add item to the buffer
    buffer->items[buffer->in] = item;
    buffer->in = (buffer->in + 1) % buffer->capacity;
    buffer->count++;

    // Signal that the buffer is not empty
    pthread_cond_signal(&buffer->not_empty);
    pthread_mutex_unlock(&buffer->mutex);

    return 0;
}

buffer_item_t buffer_get(buffer_t *buffer) {
    pthread_mutex_lock(&buffer->mutex);

    // Wait until there's at least one item in the buffer
    while (buffer->count == 0) {
        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);
    }

    // Get the item from the buffer
    buffer_item_t item = buffer->items[buffer->out];
    buffer->out = (buffer->out + 1) % buffer->capacity;
    buffer->count--;

    // Signal that the buffer is not full
    pthread_cond_signal(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);

    return item;
}