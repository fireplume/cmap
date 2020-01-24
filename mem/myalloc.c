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
#include <string.h>
#include <sys/mman.h>
#include <unistd.h> /* sysconf(3) */


#include "linkedList.h"


#ifdef MEM_DEBUG
#define PRINTV(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define PRINTV(...)
#endif


#define MEMNODE_T(o) ((memnode_t*)o)

#define _MMAP(s) mmap(NULL, s, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
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

// Terminology
// Bucket:  Represents a memory size allocation category. For examples,
//          all allocations smaller than 4Kb go into the 4Kb bucket.
// Node:    When allocations are done, they are registered into a
//          (memory) node which itself belongs to a bucket.

// This should be a power of 2 as its log2 is used to calculate the min
// bucket size.
#define MAX_ALLOC_PER_NODE 128
// This should be a multiple of MAX_ALLOC_PER_NODE as it wouldn't make
// much sense to potentially free a partially used node.
#define FREED_POINTERS_BUFFER_SIZE (2*MAX_ALLOC_PER_NODE)
/*********************************************************************/
// Following is initialized in number of system pages, will get set with
// byte values at run time.
// Must be set in ascending order, except for the last element which
// must be 0 for arbitrary size allocations (but greater than the prior
// size).
static size_t BUCKET_SIZES[] = {1, 4, 16, 64, 0};
static const unsigned int NB_BUCKETS = sizeof(BUCKET_SIZES)/sizeof(size_t);
/*********************************************************************/


typedef struct memnode_t memnode_t;
typedef struct memnode_t {
    // This must be the first member of the structure to make
    // it LinkedList compatible.
    LLNode node;

    // Block allocate address start. To simplify pointer math, using char*
    char* addr;

    // Offset of next available position within block
    char* nextAlloc;

    // Size of block in bytes
    size_t size;

    // Allocated addresses
    void* inuse[MAX_ALLOC_PER_NODE];
    unsigned short inUseIndex;

    // If allocations == releases, no memory is used by the client
    // for this node.
    unsigned allocations;
    unsigned releases;
} memnode_t;


typedef struct heap_t {
    // Protect internal structures during multithreading
    // (1) we could perhaps have one mutex per allocation size path?
    pthread_mutex_t mutex;

    LinkedList bucketNodes[NB_BUCKETS];
    unsigned int bucketNbAlloc[NB_BUCKETS];

    // Careful if multi threading multiple allocation paths with those:
    unsigned bucketSel;
    memnode_t* bufNode;
    size_t bufSize;
    char* freeBuffer[FREED_POINTERS_BUFFER_SIZE];
    unsigned short freeBufferIndex;
} heap_t;


// Our heap container
static heap_t* heap = NULL;


// Memory node linked list compare function. It is used
// to sort the nodes in ascending order of start allocation
// addresses.
int __memnodeCompare(const void* a, const void* b) {
    if( (MEMNODE_T(a))->addr > (MEMNODE_T(b))->addr ) {
        return 1;
    } else if( (MEMNODE_T(a))->addr < (MEMNODE_T(b))->addr ){
        return -1;
    }
    return 0;
}


// libmyalloc initialization
static void __attribute__((constructor)) init();
static void init() {
    fprintf(stderr, "Using custom malloc!\n");

    // Initialize heap
    heap = _MMAP(sizeof(heap_t));
    assert(heap != MAP_FAILED);

    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    LOG2(PAGE_SIZE, LOG2_PAGE_SIZE);

    for(int i=0; i<sizeof(BUCKET_SIZES)/sizeof(size_t); i++){
        BUCKET_SIZES[i] <<= LOG2_PAGE_SIZE;
    }

    // calculate minimum allocation size and its log2
    unsigned int buf = 0;

    // Get log2(MAX_ALLOC_PER_NODE)
    LOG2(MAX_ALLOC_PER_NODE, buf);

    // Set min alloc size
    MIN_ALLOC_SIZE = BUCKET_SIZES[0] >> buf;

    // Calculate log2 of minimum allocation size
    LOG2(MIN_ALLOC_SIZE, LOG2_MIN_ALLOC_SIZE);

    pthread_mutex_init(&heap->mutex, NULL);

    for(int i=0; i<NB_BUCKETS; i++) {
        ll_init(&(heap->bucketNodes[i]), __memnodeCompare);
    }
}


// libmyalloc teardown, final cleanup and stats output
static void __attribute__((destructor)) teardown();
static void teardown() {
    // Remaining unfreed memory will be reclaimed by the OS

    #ifdef QADEBUG
    fprintf(stderr, "MYALLOC: Shutdown!\n\n");
    fprintf(stderr, "malloc override statistics:\n\n");
    fprintf(stderr, "  alloc:           %-7d calls\n", (allocCount-callocCount-reallocCount));
    fprintf(stderr, "  calloc:          %-7d calls\n", callocCount);
    fprintf(stderr, "  free:            %-7d calls\n", freeCount);
    fprintf(stderr, "  realloc:         %-7d calls\n", reallocCount);
    fprintf(stderr, "  alloc'ed:        %-7d KB\n", allocedMem/1024);
    fprintf(stderr, "  freed:           %-7d KB\n", freedMem/1024);
    fprintf(stderr, "\nAllocations by memory buckets\n\n");
    int i;
    for(i=0;i<NB_BUCKETS-1;i++) {
        fprintf(stderr, "   %-6zu KB: %d\n", BUCKET_SIZES[i]>>10, heap->bucketNbAlloc[i]);
    }
    fprintf(stderr, "  >%-6zu KB: %d\n", BUCKET_SIZES[i-1]>>10, heap->bucketNbAlloc[i]);
    #endif
}


size_t NODE_SPACE_LEFT(LLNode* node) __attribute__((always_inline));
inline size_t NODE_SPACE_LEFT(LLNode* node) {
    return ( MEMNODE_T(node)->size - ( (MEMNODE_T(node)->nextAlloc) - (MEMNODE_T(node)->addr) ) );
}


void ALLOCATE(void** ptr, LLNode* node, size_t size) __attribute__((always_inline));
inline void ALLOCATE(void** ptr, LLNode* node, size_t size) {
    (*ptr) = (void*)(MEMNODE_T(node))->nextAlloc;
    MEMNODE_T(node)->nextAlloc += size;
    MEMNODE_T(node)->inuse[(MEMNODE_T(node)->inUseIndex)++] = (*ptr);
    MEMNODE_T(node)->allocations++;
}


// Map a new block of memory
void* getNewNode(const int size) {
    heap->bufSize = BUCKET_SIZES[heap->bucketSel];
    if(heap->bufSize == 0) {
        heap->bufSize = size;
    }

    heap->bufNode = (memnode_t*) _MMAP(sizeof(memnode_t) + heap->bufSize);
    PRINTV("+MEM: Mapped node: %p\n", heap->bufNode);
    if(heap->bufNode == MAP_FAILED) {
        return MAP_FAILED;
    }

    // First address available for client is at offset sizeof(memnode_t)
    heap->bufNode->addr = (char*)(heap->bufNode) + sizeof(memnode_t);
    heap->bufNode->nextAlloc = heap->bufNode->addr;
    heap->bufNode->size = heap->bufSize;
    // other members set at 0 by mmap

    ll_insert(&heap->bucketNodes[heap->bucketSel], (LLNode*)heap->bufNode);

    // If sorted, 'tail' is not necessarily 'last_insert'
    return heap->bucketNodes[heap->bucketSel].last_insert;
}


void* malloc(size_t size) {
    #ifdef QADEBUG
    atomic_add(&allocCount,1);
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
    // MIN_ALLOC_SIZE, which corresponds to index 0 of heap->bucketNodes
    heap->bucketSel -= LOG2_MIN_ALLOC_SIZE;

    // Cap bucketSel to ALLOCATION_RANGES - 1
    if(heap->bucketSel >= NB_BUCKETS) {
        heap->bucketSel = NB_BUCKETS - 1;
    }

    // Is the bucket empty?
    if(heap->bucketNodes[heap->bucketSel].last_insert == NULL) {
        if(getNewNode(size) == MAP_FAILED) {
            pthread_mutex_unlock(&heap->mutex);
            return NULL;
        }
    }

    // Does it fit in last node?
    if(size > NODE_SPACE_LEFT(heap->bucketNodes[heap->bucketSel].last_insert)) {
        if(getNewNode(size) == MAP_FAILED) {
            pthread_mutex_unlock(&heap->mutex);
            return NULL;
        }
    }

    void* ptr;
    ALLOCATE(&ptr, heap->bucketNodes[heap->bucketSel].last_insert, size);

    pthread_mutex_unlock(&heap->mutex);

    #ifdef QADEBUG
    allocedMem += size;
    heap->bucketNbAlloc[heap->bucketSel]++;
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

    // Buffer free pointers until ready to clean up
    if(heap->freeBufferIndex < FREED_POINTERS_BUFFER_SIZE-1) {
        heap->freeBuffer[heap->freeBufferIndex++] = ptr;
        pthread_mutex_unlock(&heap->mutex);
        return;
    }
    heap->freeBuffer[heap->freeBufferIndex] = ptr;

    // sort freed pointers
    register int j;
    {
        register void* tmp;
        register int i;
        for(i=0; i<FREED_POINTERS_BUFFER_SIZE; i++) {
            for(j=i+1; j<FREED_POINTERS_BUFFER_SIZE; j++) {
                if(heap->freeBuffer[j] < heap->freeBuffer[i]) {
                    tmp = heap->freeBuffer[j];
                    heap->freeBuffer[j] = heap->freeBuffer[i];
                    heap->freeBuffer[i] = tmp;
                }
            }
        }
    }

    // bulk process freed pointers
    register memnode_t* node;
    register int k;
    heap->freeBufferIndex = 0;

    for(heap->bucketSel=0; heap->bucketSel<NB_BUCKETS; heap->bucketSel++) {
        ll_reset_iterator(&heap->bucketNodes[heap->bucketSel]);
        while( ( node = (memnode_t*)ll_iter(&heap->bucketNodes[heap->bucketSel]) ) != NULL ) {
            for(k=0; k<FREED_POINTERS_BUFFER_SIZE; k++) {
                if(heap->freeBuffer[k] >= node->nextAlloc || heap->freeBuffer[k] < node->addr) {
                    continue;
                }
                for(j=0;j < node->inUseIndex; j++) {
                    if(heap->freeBuffer[k] == node->inuse[j]) {
                        node->inuse[j] = NULL;
                        heap->freeBuffer[k] = (void*)-1LL;
                        node->releases++;
                        heap->freeBufferIndex++;
                        break;
                    }
                }
            }

            // Should we release even if node not completely full?
            if(!NODE_SPACE_LEFT((LLNode*)node) && (node->releases == node->allocations) ) {
                #ifdef QADEBUG
                atomic_add(&freedMem, (node->nextAlloc - node->addr));
                #endif

                ll_del(&heap->bucketNodes[heap->bucketSel], (LLNode*)node);

                PRINTV("-MEM: munmap node: %p\n", node);
                munmap(node, (node->size+sizeof(memnode_t)));
            }

            if(heap->freeBufferIndex == FREED_POINTERS_BUFFER_SIZE) {
                heap->freeBufferIndex = 0;
                pthread_mutex_unlock(&heap->mutex);
                return;
            }
        }
    }

#ifdef QADEBUG
    for(int i=0; i<FREED_POINTERS_BUFFER_SIZE; i++) {
        if(heap->freeBuffer[i] != (void*)-1LL) {
            fprintf(stderr, "ERROR: NOT FREED: %p\n", heap->freeBuffer[i]);
        }
    }
#endif


    // Some of the pointers attempted to be freed were duds
    heap->freeBufferIndex = 0;
    pthread_mutex_unlock(&heap->mutex);

#endif
}


void *calloc(size_t nmemb, size_t size) {
    #ifdef QADEBUG
    atomic_add(&callocCount,1);
    #endif
    void* ptr = malloc(nmemb*size);
    if(ptr != NULL) {
        memset(ptr, 0, nmemb*size);
    }
    return ptr;
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



