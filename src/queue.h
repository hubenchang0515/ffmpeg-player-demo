#ifndef FFMPEG_PLAYER_DEMO_QUEUQ
#define FFMPEG_PLAYER_DEMO_QUEUQ

#include <stddef.h>
#include <stdbool.h>

typedef struct Queue Queue;

Queue* createQueue(size_t itemSize);
void deleteQueue(Queue* queue);
bool pushQueue(Queue* queue, const void* item);
void* popQueue(Queue* queue);
int countQueue(Queue* queue);

#endif // FFMPEG_PLAYER_DEMO_QUEUQ