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
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>


/*

Bare, inefficient (at least memory wise) malloc and friends override
implementation.

Memory never released until the end of the program to keep things
simple, or unless client calls 'freeCheat'.

Scheme:

We allocate blocks of memory through mmap from which we then dispense
chunks to clients.

The motivation behind this implementation was to prove that I could get
<search.h> binary tree implementation to work cross process which it
kind of, but...

valgrind happy insofar as memory leaks are concerned.

*/


#define DNODE_SIZE (sizeof(char) * 4 * 1024 * 1024 )
#define NODE_SPACE_LEFT(node) (DNODE_SIZE - ( (node->nextAlloc) - (node->addr) ) )
#define _MMAP(s) mmap(NULL, s, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0)

#define atomic_add(ptr,value) __sync_fetch_and_add(ptr,value)

#define INIT_MAPPINGS 3
#define DEFAULT_MAPPING_SYS_LIMITS 65530

// malloc statistics, only when QADEBUG is defined
#ifdef QADEBUG
static unsigned int allocCount = 0;
static unsigned int callocCount = 0;
static unsigned int freeCount = 0;
static unsigned int reallocCount = 0;
static unsigned int allocedMem = 0;
#endif


typedef struct memnode_t memnode_t;
typedef struct memnode_t {
    // Base address of memory block mapped
    char* addr;
    // Address of next available position within block
    char* nextAlloc;
    // Size of block, necessary only for when a request
    // bigger than DNODE_SIZE is made. We use this later
    // on to munmap.
    unsigned int size;
    // Pointer to next mapped block of memory, or NULL
    memnode_t* nextNode;
} memnode_t;


typedef struct heap_t {
    // Each memory block allocation is protected by this semaphore
    sem_t sem;
    // Pointer to first block of memory allowing us to free everything.
    memnode_t* firstDnode;
    // Pointer to current block of memory ready for dispensing
    // chunks of memory to client.
    memnode_t* dnode;
} heap_t;


// Our heap container
static heap_t* heap = NULL;
// Maximum number of mappings allowed per process
// Initialize to INIT_MAPPINGS so we can get this thing started.
static int sysMappingLimit = INIT_MAPPINGS;


// Map a new block of memory
void* getNewNode(size_t s) {

    sysMappingLimit -= 2;
    if(sysMappingLimit <= 0) {
        fprintf(stderr, "Almost reached maximum mapping for process, exiting\n");
        fprintf(stderr, "You can increase it with: sysctl -w vm.max_map_count=<number>\n");
        exit(-1);
    }

    memnode_t* node = (memnode_t*) _MMAP(sizeof(memnode_t));
    if(node == MAP_FAILED) {
        return MAP_FAILED;
    }
    node->addr = _MMAP(s);
    node->nextNode = NULL;
    node->nextAlloc = node->addr;
    node->size = s;

    return node;
}


// libmyalloc initialization
static void __attribute__((constructor)) init();
static void init() {
    // Initialize healp
    heap = _MMAP(sizeof(heap_t));
    assert(heap != MAP_FAILED);
    sem_init(&heap->sem, 1, 1);

    heap->dnode = getNewNode(DNODE_SIZE);
    heap->firstDnode = heap->dnode;

    // Get max mapping count
    FILE *fp;
    fp = fopen("/proc/sys/vm/max_map_count", "r");
    if(fp != NULL) {
        if(fscanf(fp, "%d", &sysMappingLimit) != 1) {

        }
    }
    #ifdef QADEBUG
    if(sysMappingLimit == 0) {
        sysMappingLimit = DEFAULT_MAPPING_SYS_LIMITS;
        fprintf(stderr, "MYALLOC: failed reading /proc/sys/vm/max_map_count, defaulting to %d!\n", sysMappingLimit);
    } else {
        fprintf(stderr, "MYALLOC: initial process mappings limit: %d!\n", sysMappingLimit);
    }
    #endif

    // Adjust for initial mappings in init
    sysMappingLimit -= INIT_MAPPINGS;
}


// libmyalloc teardown, final cleanup and stats output
static void __attribute__((destructor)) teardown();
static void teardown() {
    #ifdef QADEBUG
    fprintf(stderr, "MYALLOC: Shutdown!\n");
    #endif
    sem_wait(&heap->sem);

    // Release all memory
    memnode_t* node = heap->firstDnode;
    memnode_t* nextNode;
    while(node != NULL) {
        nextNode = node->nextNode;
        munmap(node->addr, node->size);
        node = nextNode;
    }

    heap->dnode = getNewNode(DNODE_SIZE);
    heap->firstDnode = heap->dnode;

    sem_post(&heap->sem);

    #ifdef QADEBUG
    fprintf(stderr, "\nmalloc override statistics:\n\n");
    fprintf(stderr, "  alloc:           %-7d calls\n", (allocCount-callocCount-reallocCount));
    fprintf(stderr, "  calloc:          %-7d calls\n", callocCount);
    fprintf(stderr, "  free:            %-7d calls\n", freeCount);
    fprintf(stderr, "  realloc:         %-7d calls\n", reallocCount);
    fprintf(stderr, "  alloced:         %-7d MB\n", allocedMem/1024/1024);
    fprintf(stderr, "  mappings left:   %-7d\n", sysMappingLimit);
    #endif
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

    // Round size to next multiple of 16bytes
    size = (((size+15)>>4)<<4);

    sem_wait(&heap->sem);

    // Does it fit in current node?
    if(size > NODE_SPACE_LEFT(heap->dnode)) {
        memnode_t* old = heap->dnode;

        if(size > DNODE_SIZE) {
            heap->dnode = getNewNode(size);
        } else {
            heap->dnode = getNewNode(DNODE_SIZE);
        }
        if(heap->dnode == MAP_FAILED) {
            return NULL;
        }
        old->nextNode = heap->dnode;
    }

    void* ptr = heap->dnode->nextAlloc;
    heap->dnode->nextAlloc += size;

    sem_post(&heap->sem);

    assert(((long long int)ptr & 0xF) == 0);

    #ifdef QADEBUG
    allocedMem += size;
    #endif
    return ptr;
}


void free(void* ptr) {
    // Ignored in this cheap implementation
    // Memory will be freed in library's destructor or on demand (see above)
    #ifdef QADEBUG
    atomic_add(&freeCount,1);
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
