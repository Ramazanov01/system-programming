#include "queue.h"

void initQueue(SatelliteQueue* q) {
    q->size = 0;
}

void enqueue(SatelliteQueue* q, Satellite s) {
    int i = q->size++;
    
    // Sort by priority in descending order (higher priority first)
    while (i > 0 && q->data[i-1].priority < s.priority) {
        q->data[i] = q->data[i-1];
        i--;
    }
    q->data[i] = s;
}

Satellite dequeue(SatelliteQueue* q) {
    // Return the highest priority element (at index 0)
    Satellite sat = q->data[0];
    
    // Shift remaining elements
    for (int i = 0; i < q->size - 1; i++) {
        q->data[i] = q->data[i + 1];
    }
    
    q->size--;
    return sat;
}

int isEmpty(SatelliteQueue* q) {
    return q->size == 0;
}

void destroyQueue(SatelliteQueue* q) {
    q->size = 0;
}

void removeSatellite(SatelliteQueue* q, int id) {
    int i;
    for (i = 0; i < q->size; ++i) {
        if (q->data[i].id == id) break;
    }
    
    if (i < q->size) {
        for (; i < q->size - 1; ++i) {
            q->data[i] = q->data[i + 1];
        }
        q->size--;
    }
}