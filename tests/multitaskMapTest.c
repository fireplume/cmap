/*********************************************************************************
MIT License

Copyright (c) 2019 Mathieu Comeau

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*********************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tmap.h"

#define MAX_KEY_SIZE 16


char** gKeys;


// KEY_FORMAT can accomodate a billion keys, we should be fine.
#define KEY_FORMAT "%09d"


typedef struct ThreadParam {
    tmap* map;
    int id;
    int nbElemPerThread;
    pthread_barrier_t* barrierWaitThreadLaunch;
    pthread_barrier_t* barrierWaitChildStart;
} ThreadParam;


// Key compare function
static int
compare(const void* pa, const void* pb)
{
    return strcmp(((tnode*)pa)->key, ((tnode*)pb)->key);
}


// Verify the map was correctly set by the threads
// If you run this test with '-s' option, it will crash the program.
int verify(tmap* map, const int nbTasks, const int nbElementsPerTask) {
    void* value;
    printf("Verification\n");
    int errors = 0;

    for(int i=0; i<nbTasks*nbElementsPerTask; i++) {
        value = tget(map, gKeys[i]);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
        if(value != (void*)i) {
            printf("ERROR: %s -> %p expected %p\n", gKeys[i], value, (void*)i);
#pragma GCC diagnostic pop
            errors++;
        }
    }

    return errors != 0;
}


// In this function the thread set their respective are of the map for non
// overlapping ranges. Values set are: value = key
void* threadWork(void* args) {
    ThreadParam* pArgs = (ThreadParam*)args;
    tmap* map = pArgs->map;
    const int id = pArgs->id;
    const int nbElemPerThread = pArgs->nbElemPerThread;
    pthread_barrier_t* threadLaunchSyncBarrier = pArgs->barrierWaitThreadLaunch;
    pthread_barrier_t* threadStartSyncBarrier = pArgs->barrierWaitChildStart;

    int start = id*nbElemPerThread;
    int end = start + nbElemPerThread - 1;
    printf("Thread %d created: Key: %d -> %d\n", id, start, end);

    pthread_barrier_wait(threadLaunchSyncBarrier);

    // Wait for all processes to be ready to work at the same time
    pthread_barrier_wait(threadStartSyncBarrier);
    printf("Thread %d working!\n", id);

    int i=0;
    for(i=start; i<=end; i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
        tadd(map, gKeys[i], (void*)i);
        tdel(map, gKeys[i]);
        tadd(map, gKeys[i], (void*)i);
#pragma GCC diagnostic pop
    }

    printf("Thread %d completed!\n", id);

    return NULL;
}


// Initialize the map to values corresponding to twice the index value.
void setKeyMem(const int totalNbElements, const int keySize) {
    #if 1
    // This does not work with my implementation of malloc but works
    // fine with glibc's
    size_t memsize = totalNbElements*sizeof(char*)+MAX_KEY_SIZE*totalNbElements;
    gKeys = (char**)malloc(memsize);
    assert(gKeys!=NULL);

    int baseOffset = totalNbElements*sizeof(char*);
    for(int i=0; i<totalNbElements; i++) {
        gKeys[i] = (char*)gKeys + baseOffset + i * keySize;
        snprintf(gKeys[i], keySize, KEY_FORMAT, i*2);
    }
    #else
    // But this does.
    size_t memsize = totalNbElements*sizeof(char*);
    gKeys = (char**)malloc(memsize);
    assert(gKeys!=NULL);

    for(int i=0; i<totalNbElements; i++) {
        gKeys[i] = (char*)malloc(MAX_KEY_SIZE);
        snprintf(gKeys[i], keySize, KEY_FORMAT, i*2);
    }
    #endif
}


void freeKeyMem() {
    free(gKeys);
}


// Launch a bunch of threads waiting at a barrier, when they've
// all reached it, unblock them all and let them do their work.
// Wait for all threads to complete and verify the results.
void multithreadTest(const int nbThreads, int nbElemPerThread, const int singleThreadedMode) {
    pthread_t* threads = malloc(nbThreads*sizeof(pthread_t));
    pthread_barrier_t barrierWaitThreadLaunch;
    pthread_barrier_t barrierWaitChildStart;

    pthread_barrier_init(&barrierWaitThreadLaunch, NULL, nbThreads+1);
    pthread_barrier_init(&barrierWaitChildStart, NULL, nbThreads+1);

    setKeyMem(nbThreads*nbElemPerThread, MAX_KEY_SIZE);

    // Our map (string to string)
    tmap* map;

    if(singleThreadedMode) {
        // This is to show that using multithreaded for the map
        // configured in single threaded mode will crash the program.
        map = tinit(compare, TMAP_NO_OVERWRITE, SINGLE_THREADED);
    } else {
        map = tinit(compare, TMAP_NO_OVERWRITE, MULTI_THREAD_SAFE);
    }

    ThreadParam* pThreadArgs = malloc(sizeof(ThreadParam)*nbThreads);
    for(int i=0; i<nbThreads; i++) {
        pThreadArgs[i].map = map;
        pThreadArgs[i].nbElemPerThread = nbElemPerThread;
        pThreadArgs[i].barrierWaitThreadLaunch = &barrierWaitThreadLaunch;
        pThreadArgs[i].barrierWaitChildStart = &barrierWaitChildStart;
    }

    for(int tid=0; tid<nbThreads; tid++) {
        pThreadArgs[tid].id = tid;
        pthread_create(&threads[tid], NULL, threadWork, &pThreadArgs[tid]);
    }

    printf("Wait for threads to launch...\n");
    pthread_barrier_wait(&barrierWaitThreadLaunch);

    printf("Unblocking all threads\n");
    pthread_barrier_wait(&barrierWaitChildStart);

    // Wait for processes to complete
    clock_t tClock = clock();
    for(int tid=0; tid<nbThreads; tid++) {
        pthread_join(threads[tid], NULL);
    }
    tClock = clock() - tClock;
    fprintf(stderr, "[%-5d*%d] Map init time:     %-3.2f seconds\n",
            nbThreads, nbElemPerThread, (float)tClock/CLOCKS_PER_SEC);

    // Verification
    tClock = clock();
    int error = verify(map, nbThreads, nbElemPerThread);
    tClock = clock() - tClock;
    fprintf(stderr, "[%-5d*%d] Verification time: %-3.2f seconds\n",
            nbThreads, nbElemPerThread, (float)tClock/CLOCKS_PER_SEC);
    if(error==0) {
        fprintf(stderr, "PASS\n");
    } else {
        fprintf(stderr, "FAIL\n");
    }

    // Memory cleanup
    tfree(map);

    freeKeyMem();
    pthread_barrier_destroy(&barrierWaitThreadLaunch);
    pthread_barrier_destroy(&barrierWaitChildStart);
    free(threads);
    free(pThreadArgs);
}
