#ifndef QUEUE_H
#define QUEUE_H

#define MAX_QUEUE 100

typedef struct {
    int id;
    int priority;
} Satellite;

typedef struct {
    Satellite data[MAX_QUEUE];
    int size;
} SatelliteQueue;

void initQueue(SatelliteQueue* q);
void enqueue(SatelliteQueue* q, Satellite s);
Satellite dequeue(SatelliteQueue* q);
int isEmpty(SatelliteQueue* q);
void destroyQueue(SatelliteQueue* q);
void removeSatellite(SatelliteQueue* q, int id);

#endif