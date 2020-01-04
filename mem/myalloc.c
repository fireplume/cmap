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
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h> /* sysconf(3) */


#define NODE_SPACE_LEFT(node) ( node->size - ( (node->nextAlloc) - (node->addr) ) )
#define ALLOCATE(ptr,node,size) \
    do { \
        ptr = (void*)node->nextAlloc; \
        node->nextAlloc += size; \
        node->inuse[(node->inUseIndex)++] = ptr; \
        node->allocations++; \
    } while(0)

#define _MMAP(s) mmap(NULL, s, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0)
#define atomic_add(ptr,value) __sync_fetch_and_add(ptr,value)
// Process log2 of a power of 2 number, 'n', set value in 'ret'
#define LOG2(n,ret) do { ret=0; while((1LL<<ret)<n) { ++ret; } } while(0)

static unsigned PAGE_SIZE = 0;
static unsigned LOG2_PAGE_SIZE = 0;
static unsigned MIN_ALLOC_SIZE = 0;
static unsigned LOG2_MIN_ALLOC_SIZE = 0;


// malloc statistics, only when QADEBUG is defined
#ifdef QADEBUG
static unsigned int allocCount = 0;
static unsigned int callocCount = 0;
static unsigned int freeCount = 0;
static unsigned int reallocCount = 0;
static unsigned int allocedMem = 0;
static unsigned int freedMem = 0;
#endif


// Be careful if tweaking those values. These were chosen so that the maximum overhead for managing
// 4096 bytes is less than or equal to 4096 bytes.
// This should be a power of 2.
#define ALLOCATIONS_PER_NODE 128
/*********************************************************************/
// Following is initialized in number of system pages, will get set with byte values at run time.
// Must be set in ascending order, except for the last element which must be -1 for
// arbitrary size allocations.
// -1 denotes any sizes greater than the prior size
static int NODE_SIZES[] = {1, 4, 16, 64, -1};
static const int NB_NODE_SIZES = sizeof(NODE_SIZES)/sizeof(int);
/*********************************************************************/


typedef struct memnode_t memnode_t;
typedef struct memnode_t {
    // Block allocate address start. To simplify pointer math, using char*
    char* addr;

    // Pointer to previous mapped block of memory, or NULL
    memnode_t* previous;
    memnode_t* next;

    // Offset of next available position within block
    char* nextAlloc;

    // Size of block
    size_t size;

    // Allocated addresses
    void* inuse[ALLOCATIONS_PER_NODE+1]; // [ALLOCATIONS_PER_NODE+1] shall always be zero
    unsigned short inUseIndex;

    // If allocations == releases and NODE_SPACE_LEFT(memnode_t*) == 0, we can unmap the node
    unsigned allocations;
    unsigned releases;
} memnode_t;


typedef struct heap_t {
    // Protect internal structures during multithreading
    // (1) we could perhaps have one mutex per allocation size path?
    pthread_mutex_t mutex;

    memnode_t* lastAllocNodes[NB_NODE_SIZES];
    unsigned int nbAlloc[NB_NODE_SIZES];

    // Careful: would need their corresponding bucket mutex if going forward with (1).
    unsigned bucketSel;

    char* freeBuffer[ALLOCATIONS_PER_NODE];
    unsigned short freeBufferIndex;
} heap_t;


// Our heap container
static heap_t* heap = NULL;


// libmyalloc initialization
static void __attribute__((constructor)) init();
static void init() {
    // Initialize heap
    heap = _MMAP(sizeof(heap_t));
    assert(heap != MAP_FAILED);

    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    LOG2(PAGE_SIZE, LOG2_PAGE_SIZE);

    for(int i=0; i<sizeof(NODE_SIZES)/sizeof(int); i++){
        NODE_SIZES[i] <<= LOG2_PAGE_SIZE;
    }

    // calculate minimum allocation size and its log2
    int buf = 0;

    // Get log2(ALLOCATIONS_PER_NODE)
    LOG2(ALLOCATIONS_PER_NODE, buf);

    // Set min alloc size
    MIN_ALLOC_SIZE = NODE_SIZES[0] >> buf;

    // Calculate log2 of minimum allocation size
    LOG2(MIN_ALLOC_SIZE, LOG2_MIN_ALLOC_SIZE);

    pthread_mutex_init(&heap->mutex, NULL);

    for(int i=0; i<NB_NODE_SIZES; i++) {
        heap->lastAllocNodes[i] = NULL;
    }
}


// libmyalloc teardown, final cleanup and stats output
static void __attribute__((destructor)) teardown();
static void teardown() {
    #ifdef QADEBUG
    fprintf(stderr, "MYALLOC: Shutdown!\n");
    #endif
    pthread_mutex_lock(&heap->mutex);

    // Release all memory
    // Is it needed at this point? What if the lib remains opened?

    pthread_mutex_unlock(&heap->mutex);

    #ifdef QADEBUG
    fprintf(stderr, "\nmalloc override statistics:\n\n");
    fprintf(stderr, "  alloc:           %-7d calls\n", (allocCount-callocCount-reallocCount));
    fprintf(stderr, "  calloc:          %-7d calls\n", callocCount);
    fprintf(stderr, "  free:            %-7d calls\n", freeCount);
    fprintf(stderr, "  realloc:         %-7d calls\n", reallocCount);
    fprintf(stderr, "  alloc'ed:        %-7d KB\n", allocedMem/1024);
    fprintf(stderr, "  freed:           %-7d KB\n", freedMem/1024);
    fprintf(stderr, "\nAllocations by memory buckets\n\n");
    int i;
    for(i=0;i<NB_NODE_SIZES-1;i++) {
        fprintf(stderr, "   %-6d KB: %d\n", NODE_SIZES[i]>>10, heap->nbAlloc[i]);
    }
    fprintf(stderr, "  >%-6d KB: %d\n", NODE_SIZES[i-1]>>10, heap->nbAlloc[i]);
    #endif
}


// Map a new block of memory
void* getNewNode(memnode_t* lastNode, const int bucketSel, const int size) {
    size_t nodeSize;
    if(bucketSel < NB_NODE_SIZES-1) {
        nodeSize = NODE_SIZES[bucketSel];
    } else {
        nodeSize = size;
    }

    memnode_t* node = (memnode_t*) _MMAP(sizeof(memnode_t) + nodeSize);
    if(node == MAP_FAILED) {
        return MAP_FAILED;
    }

    // First address available for client is at offset sizeof(memnode_t)
    node->addr = (char*)node + sizeof(memnode_t);
    node->previous = lastNode;
    if(lastNode != NULL) {
        lastNode->next = node;
    }
    node->next = NULL;
    node->nextAlloc = node->addr;
    node->size = nodeSize;
    // other members set at 0 by mmap

    return node;
}


void* malloc(size_t size) {
    #ifdef QADEBUG
    atomic_add(&allocCount,1);
    static int __flag = 0;
    if(!__flag) {
        __flag = 1;
        puts("Using custom malloc!");
    }
    #endif

    if(size == 0) {
        return NULL;
    }

    // Round size to next multiple of minimum allocations
    size = (((size+MIN_ALLOC_SIZE-1)>>LOG2_MIN_ALLOC_SIZE)<<LOG2_MIN_ALLOC_SIZE);

    pthread_mutex_lock(&heap->mutex);

    heap->bucketSel = 0;
    LOG2(size, heap->bucketSel);

    // We subtract LOG2_MIN_ALLOC_SIZE because our node allocation unit size starts at
    // MIN_ALLOC_SIZE, which corresponds to index 0 of heap->lastAllocNodes
    heap->bucketSel -= LOG2_MIN_ALLOC_SIZE;

    // Cap block selector to ALLOCATION_RANGES - 1
    if(heap->bucketSel >= NB_NODE_SIZES) {
        heap->bucketSel = NB_NODE_SIZES - 1;
    }

    // Does the node exist?
    if(heap->lastAllocNodes[heap->bucketSel] == NULL) {
        heap->lastAllocNodes[heap->bucketSel] = getNewNode(NULL, heap->bucketSel, size);
    }

    // Does it fit in last node?
    if(size > NODE_SPACE_LEFT(heap->lastAllocNodes[heap->bucketSel])) {
        heap->lastAllocNodes[heap->bucketSel] = getNewNode(heap->lastAllocNodes[heap->bucketSel],
                                                           heap->bucketSel,
                                                           size);

        if(heap->lastAllocNodes[heap->bucketSel] == MAP_FAILED) {
            return NULL;
        }
    }

    void* ptr;
    ALLOCATE(ptr, heap->lastAllocNodes[heap->bucketSel], size);

    pthread_mutex_unlock(&heap->mutex);

    #ifdef QADEBUG
    allocedMem += size;
    heap->nbAlloc[heap->bucketSel]++;
    #endif

    return ptr;
}


void free(void* ptr) {
    if(ptr == NULL || ptr == MAP_FAILED) {
        return;
    }

    #ifdef QADEBUG
    atomic_add(&freeCount,1);
    #endif

    // Buffering frees would improve performance

#ifndef FAST_ALLOC
    pthread_mutex_lock(&heap->mutex);

    // check if in a node first?

    if(heap->freeBufferIndex < ALLOCATIONS_PER_NODE-1) {
        heap->freeBuffer[heap->freeBufferIndex++] = ptr;
        pthread_mutex_unlock(&heap->mutex);
        return;
    }
    heap->freeBuffer[heap->freeBufferIndex] = ptr;

    // sort freed pointers
    register int i;
    register int j;
    register void* tmp;
    for(int i=0; i<ALLOCATIONS_PER_NODE; i++) {
        for(j=i+1; j<ALLOCATIONS_PER_NODE; j++) {
            if(heap->freeBuffer[j] < heap->freeBuffer[i]) {
                tmp = heap->freeBuffer[j];
                heap->freeBuffer[j] = heap->freeBuffer[i];
                heap->freeBuffer[i] = tmp;
            }
        }
    }

    // bulk process freed pointers
    register memnode_t* node;
    register memnode_t* tmpnode;
    register int k;
    heap->freeBufferIndex = 0;

    for(i=0; i<NB_NODE_SIZES; i++) {
        node = heap->lastAllocNodes[i];
        while(node != NULL) {
            if(heap->freeBuffer[0] > node->nextAlloc || heap->freeBuffer[ALLOCATIONS_PER_NODE-1] < node->addr) {
                node = node->previous;
                continue;
            }

            // We know ptr is in range, but not necessarily valid
            for(j=0;j < node->inUseIndex; j++) {
                for(k=0; k<ALLOCATIONS_PER_NODE; k++) {
                    if(heap->freeBuffer[k] == node->inuse[j]) {
                        node->inuse[j] = NULL;
                        node->releases++;
                        heap->freeBufferIndex++;
                        break;
                    }
                }
            }

            // If half or more of the node is used and freed, unmap it
            if(!NODE_SPACE_LEFT(node) && (node->releases == node->allocations) ) {
                #ifdef QADEBUG
                atomic_add(&freedMem, (node->nextAlloc - node->addr));
                #endif

                if(heap->lastAllocNodes[i] == node) {
                    heap->lastAllocNodes[i] = node->previous;
                }

                if(node->previous != NULL) {
                    node->previous->next = node->next;
                }
                if(node->next != NULL) {
                    node->next->previous= node->previous;
                }

                tmpnode = node;
                node = node->previous;
                munmap(tmpnode, tmpnode->size);
            } else {
                node = node->previous;
            }

            if(heap->freeBufferIndex == ALLOCATIONS_PER_NODE) {
                heap->freeBufferIndex = 0;
                pthread_mutex_unlock(&heap->mutex);
                return;
            }
        }
    }

    // Some of the pointers attempted to be freed were duds
    heap->freeBufferIndex = 0;
    pthread_mutex_unlock(&heap->mutex);

#endif
}


void *calloc(size_t nmemb, size_t size) {
    #ifdef QADEBUG
    atomic_add(&callocCount,1);
    #endif
    return malloc(nmemb*size);
}


void *realloc(void *ptr, size_t size) {
    #ifdef QADEBUG
    atomic_add(&reallocCount,1);
    #endif

    if (ptr == NULL) {
        return malloc(size);
    } else if (size == 0) {
        free(ptr);
        return NULL;
    }

    free(ptr);
    ptr = malloc(size);

    return ptr;
}
