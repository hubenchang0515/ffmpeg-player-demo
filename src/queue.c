#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue.h"

typedef struct Queue{
    void* data;
    size_t itemSize;
    size_t count;
}Queue;

Queue* createQueue(size_t itemSize)
{
    Queue* queue = malloc(sizeof(Queue));
    if (queue == NULL)
    {
        fprintf(stderr,  "%s:%d bad alloc\n", __FILE__, __LINE__);
        return NULL;
    }

    queue->data = NULL;
    queue->itemSize = itemSize;
    queue->count = 0;
    return queue;
}

void deleteQueue(Queue* queue)
{
    if (queue == NULL)
        return;
    
    if (queue->data != NULL)
        free(queue->data);

    free(queue);
}

bool pushQueue(Queue* queue, const void* item)
{
    if (queue == NULL || item == NULL)
        return false;
    
    void* data = realloc(queue->data, queue->itemSize * (queue->count + 1));
    if (data == NULL)
    {
        fprintf(stderr,  "%s:%d bad alloc\n", __FILE__, __LINE__);
        return false;
    }

    queue->data = data;
    memcpy(data + queue->itemSize * queue->count, item, queue->itemSize);
    queue->count += 1;
    return true;
}

void* popQueue(Queue* queue)
{
    if (queue == NULL || queue->data == NULL)
        return NULL;

    void* item = malloc(queue->itemSize);
    memcpy(item, queue->data, queue->itemSize);
    memcpy(queue->data, queue->data + queue->itemSize, queue->itemSize * (queue->count - 1));
    queue->data = realloc(queue->data, queue->itemSize * (queue->count - 1));
    queue->count -= 1;

    return item;
}