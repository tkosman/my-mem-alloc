#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <stdlib.h> 

#define CHUNK_SIZE sizeof(heapChunk) //? defining size of one chunk
#define ALIGNMENT 8 //? I'll align each chunk by 8 bytes
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1)) //? this is a macro for finding the next multiple of 8
#define MAGIC_NUMBER 0x12345678 //? used for identifying the chunk

#define heapAlloc(bytes) allocWithStats(bytes, extractFilename(__FILE__), __LINE__)

pthread_mutex_t global_malloc_lock;

//? this struct stores information about a single chunk
typedef struct heapChunk
{
    int magic;
    size_t size;
    bool isFree;
    struct heapChunk *next;

    const char *file;
    int line;

} heapChunk;

typedef struct {
    int allocCalls;         
    size_t totalAllocated;   
    size_t peakMemory;
    size_t sbrkCalls;
    int corruptedChunks;
    int unfreedChunks;
} MemoryStats;

MemoryStats mem_stats = {0};

//? for storing the all chunks
heapChunk *allChunks = NULL;

//? simple linear search for finding a next free chunk
heapChunk *findFreeChunk(heapChunk **last, size_t size)
{
    heapChunk *current = allChunks;
    
    while(current && !(current->isFree && current->size >= size))
    {
        *last = current;
        current = current->next;
    }
    return current;
}

void splitChunk(heapChunk *chunk, size_t size)
{
    heapChunk *newChunk = (heapChunk *)((char *)chunk + size + CHUNK_SIZE);
    newChunk->size = chunk->size - size - CHUNK_SIZE;
    newChunk->isFree = true;
    newChunk->next = chunk->next;
    newChunk->magic = MAGIC_NUMBER;

    chunk->size = size;
    chunk->next = newChunk;
}

void *sbrkWithStats(size_t size)
{
    mem_stats.sbrkCalls++;
    return sbrk(size);
}

void *myAlloc(size_t size, const char *file, int line)
{
    if (size <= 0) return NULL;

    size_t totalSize;
    heapChunk *chunk = NULL;
    heapChunk *last = NULL;

    pthread_mutex_lock(&global_malloc_lock);

    size = ALIGN(size);
    totalSize = size + CHUNK_SIZE;

    if(allChunks)
    {
        last = allChunks;
        chunk = findFreeChunk(&last, size);
        if(chunk)
        {
                
            if(chunk->size - size >= CHUNK_SIZE + ALIGNMENT)
            {
                splitChunk(chunk, size);
            }
            chunk->isFree = false;
        }
    }

    if (!chunk)
    {
        chunk = sbrkWithStats(totalSize);

        if (chunk == (void*) -1)
        {
            pthread_mutex_unlock(&global_malloc_lock);
            return NULL;
        }

        chunk->size = size;
        chunk->magic = MAGIC_NUMBER;
        chunk->next = NULL;
        chunk->isFree = false;

        if (last)
        {
            last->next = chunk;
        }
        else
        {
            allChunks = chunk;
        }
    }

    pthread_mutex_unlock(&global_malloc_lock);


    chunk->file = file;
    chunk->line = line;
    return (void*)(chunk + 1);
}


//? function extending heapAlloc to take file, line and stats
void *allocWithStats(size_t bytes, const char *file, int line)
{
    mem_stats.allocCalls++;
    mem_stats.totalAllocated += bytes;
    if (mem_stats.totalAllocated > mem_stats.peakMemory) {
        mem_stats.peakMemory = mem_stats.totalAllocated;
    }
    return myAlloc(bytes, file, line);
}

void heapMergeChunks(heapChunk *chunk)
{
    while (chunk->next && chunk->next->isFree)
    {
        chunk->size += CHUNK_SIZE + chunk->next->size;
        chunk->next = chunk->next->next;
    }
}

void heapFree(void* ptr)
{
    if (!ptr) return;

    heapChunk *chunk = (heapChunk*)ptr - 1;

    pthread_mutex_lock(&global_malloc_lock);

    if(chunk->magic != MAGIC_NUMBER)
    {
        mem_stats.corruptedChunks++;
        pthread_mutex_unlock(&global_malloc_lock);
        perror("Memory corruption");
    }

    chunk->isFree = true;
    if (chunk->next)
    {
        heapMergeChunks(chunk);
    }
    pthread_mutex_unlock(&global_malloc_lock);
}

const char* extractFilename(const char *path)
{
    const char *filename = path;
    for (const char *p = path; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            filename = p + 1;
        }
    }
    return filename;
}

void checkForUnfreedChunks()
{
    heapChunk *current = allChunks;
    while (current)
    {
        if (!current->isFree)
        {
            mem_stats.unfreedChunks++;
            printf("Unfreed chunk: %p, Allocator: %s, Line: %d\n", current, current->file, current->line);
        }
        current = current->next;
    }
}

void printAll()
{
    heapChunk *current = allChunks;
    while (current != NULL)
    {
        printf("Chunk at address: %p, \n\tSize: %zu \n\tLine: %d \n\tFile: %s \n\tFree: %s\n", 
                (void *)current, 
                current->size, 
                current->line,
                current->file,
                current->isFree ? "Yes" : "No");

        current = current->next;
        printf("\n");
    }
    printf("###############################\n");
}

void printMemoryStats()
{
    printf("Allocation calls: %d\n", mem_stats.allocCalls);
    printf("Allocated in total: %zu bites\n", mem_stats.totalAllocated);
    printf("Peek usage: %zu bites\n", mem_stats.peakMemory);
    printf("Calls of sbrk: %zu\n", mem_stats.sbrkCalls);
    printf("Corrupted chunks: %d\n", mem_stats.corruptedChunks);
    printf("Unfreed chunks: %d\n", mem_stats.unfreedChunks);
}


int main(void)
{
    atexit(checkForUnfreedChunks);
    pthread_mutex_init(&global_malloc_lock, NULL);

    void *ptr = heapAlloc(5);
    void *ptr3 = heapAlloc(60);
    void *ptr2 = heapAlloc(5);
    heapFree(ptr3);
    void *ptr4 = heapAlloc(5);
    heapFree(ptr);
    heapFree(ptr2);
    // heapFree(ptr4);
    printMemoryStats();


    pthread_mutex_destroy(&global_malloc_lock);
    return 0;
}