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
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tmap.h"

#ifndef NODE_BLOCK_NB_ELEMENTS
#define NODE_BLOCK_NB_ELEMENTS 2*1024
#endif


#ifdef QADEBUG
#define PRINTD(...) do { fprintf(stderr, "DEBUG: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0);
#else
#define PRINTD(...)
#endif

// In case client allocator uses mmap:
#ifndef MAP_FAILED
#define MAP_FAILED ((void*)-1)
#endif

/*************************** INTERNAL *********************************/
// Internal functions, thou shall not use syncing primitives
// within internal functions
/**********************************************************************/


static void* (*__tmyalloc)(size_t) = NULL;
static void  (*__tmyfree)(void*, size_t) = NULL;


#define MYALLOC(s) __tmyalloc(s)
#define MYFREE(p,s) __tmyfree(p,s)

#define PTR_OFFSET(p,offset) (((char*)p)+offset)

#define EXTRA_BLOCK_ALLOCATION 1
#define FIRST_BLOCK_ALLOCATION 0

#define NODE_BLOCK_DELETED -1


/**********************************************************************/
// Syncing symbols for protecting map in multitasking context
void __tSyncWait(tmap* map) __attribute__((always_inline));
inline void __tSyncWait(tmap* map) {
    if(map->__multitask == MULTI_THREAD_SAFE) {
        pthread_mutex_lock(map->__mutex);
    }
}


void __tSyncPost(tmap* map) __attribute__((always_inline));
inline void __tSyncPost(tmap* map) {
    if(map->__multitask == MULTI_THREAD_SAFE) {
        pthread_mutex_unlock(map->__mutex);
    }
}


void __nodeBlockAlloc(tmap* map) {
    tnodeblock* oldNodeBlock = map->__currentNodeBlock;

    map->__currentNodeBlock = (tnodeblock*)MYALLOC( sizeof(tnodeblock) + NODE_BLOCK_NB_ELEMENTS*sizeof(tnode) );
    map->__currentNodeBlock->__nodes = (tnode*)PTR_OFFSET(map->__currentNodeBlock, sizeof(tnodeblock));
    map->__currentNodeBlock->__index = 0;
    map->__currentNodeBlock->__next = NULL;
    map->__currentNodeBlock->__previous = oldNodeBlock;
    map->__currentNodeBlock->__activeNodes = 0;

    // Make old node block point to new block
    if(oldNodeBlock != NULL) {
        oldNodeBlock->__next = map->__currentNodeBlock;
    }
}


void __nodeBlockRelease(tmap* map, tnodeblock* nodeBlock) {
    if(map->__firstNodeBlock == nodeBlock) {
        map->__firstNodeBlock = nodeBlock->__next;
    }
    if(map->__currentNodeBlock == nodeBlock) {
        map->__currentNodeBlock = nodeBlock->__previous;
    }

    // Make previous block and next block around 'nodeBlock'
    // point at each other
    if(nodeBlock->__previous != NULL) {
        nodeBlock->__previous->__next = nodeBlock->__next;
    }
    if(nodeBlock->__next != NULL) {
        nodeBlock->__next->__previous = nodeBlock->__previous;
    }
    // Free memory
    MYFREE(nodeBlock, sizeof(tnodeblock)+NODE_BLOCK_NB_ELEMENTS*sizeof(tnode));

    if(map->__firstNodeBlock == NULL) {
        __nodeBlockAlloc(map);
        map->__firstNodeBlock = map->__currentNodeBlock;
    }
}


tnode* __tget(tmap* map, void* key) __attribute__((always_inline));
inline tnode* __tget(tmap* map, void* key) {
    tnode** pNode;
    pNode = tfind(&key, &map->__root, map->__cmp);
    if(pNode != NULL) {
        return *pNode;
    }
    return NULL;
}


int __tdel(tmap* map, void* key) __attribute__((always_inline));
inline int __tdel(tmap* map, void* key) {
#ifdef FAST_MAP
    // When compiled with FAST_MAP, memory is released only
    // when tfree is called, giving a =~ 25% init time performance
    // increase.
    tdelete(&key, &map->__root, map->__cmp);
#else
    tnode* pnode;

    pnode = __tget(map, key);

    if(pnode != NULL) {
        tdelete(&key, &map->__root, map->__cmp);

        pnode->__mynodeblock->__activeNodes--;

        // Mark node as deleted by setting its key to 0
        pnode->key = 0;
        if(pnode->__mynodeblock->__activeNodes == 0 && pnode->__mynodeblock->__index >= NODE_BLOCK_NB_ELEMENTS) {
            __nodeBlockRelease(map, pnode->__mynodeblock);
            return NODE_BLOCK_DELETED;
        }
    }
#endif
    return 0;
}


void __free(void* ptr, size_t s) {
    free(ptr);
}


void* __malloc(size_t s) {
    return malloc(s);
}


void __tallocator_init(tallocator* allocator, const int multitaskMode) {
    if(allocator == NULL) {
        __tmyalloc = __malloc;
        __tmyfree = __free;
    } else {
        __tmyalloc = allocator->tmyalloc;
        __tmyfree = allocator->tmyfree;
    }
}


/*************************** PUBLIC **********************************/
// Public functions
/**********************************************************************/


void tconf(tallocator* allocator) {
    if(allocator == NULL) {
        return;
    }
    __tallocator_init(allocator, -1);
}


tmap* tinit(int (*cmp)(const void*, const void*),
            const int noOverwrite,
            const int multitask) {
    tmap* map;

    if(__tmyalloc == NULL) {
        __tallocator_init(NULL, multitask);
    }

    map = MYALLOC(sizeof(tmap));
    map->__multitask = multitask;
    map->__cmp = cmp;
    map->__root = NULL;
    map->__firstNodeBlock = NULL;
    map->__currentNodeBlock = NULL;
    map->__noOverwrite = noOverwrite;

    // Allocate an initial node block
    __nodeBlockAlloc(map);
    // Remember first block for later memory release
    map->__firstNodeBlock = map->__currentNodeBlock;

    map->__mutex = NULL;

    if(multitask == MULTI_THREAD_SAFE) {
        map->__mutex = MYALLOC(sizeof(pthread_mutex_t));
        pthread_mutex_init(map->__mutex, NULL);
    } else if (multitask != SINGLE_THREADED) {
        fprintf(stderr, "Unsupported multitask parameter: %d\n", multitask);
        exit(-1);
    }

    return map;
}


// Override compare function
void tsetcmp(tmap* map,
             int (*cmp)(const void*, const void*)) {
    map->__cmp = cmp;
}


// To be used by client for twalk
void* troot(tmap* map) {
    return map->__root;
}


// Remove a node from the binary tree
void tdel(tmap* map, void* key) {
    __tSyncWait(map);
    __tdel(map, key);
    __tSyncPost(map);
}


// Release all internal memory associated with the map
// It is up to the client to release memory associated
// with the keys and corresponding values.
void tfree(tmap* map) {
    tnodeblock* nodeBlock = map->__firstNodeBlock;
    tnodeblock* nextNodeBlock;
    int blockStatus;
    register int node;

    while(nodeBlock != NULL) {
        nextNodeBlock = nodeBlock->__next;

        // Remove nodes from bineary tree
        for(node=0; node < nodeBlock->__index; ++node) {
            if(nodeBlock->__nodes[node].key == 0) {
                continue;
            }
            blockStatus = __tdel(map, nodeBlock->__nodes[node].key);
            if(blockStatus == NODE_BLOCK_DELETED) {
                break;
            }
        }

        if(blockStatus != NODE_BLOCK_DELETED) {
            MYFREE(nodeBlock, sizeof(tnodeblock)+NODE_BLOCK_NB_ELEMENTS*sizeof(tnode));
        }
        nodeBlock = nextNodeBlock;
    }

    // release synchronization object
    if(map->__multitask == MULTI_THREAD_SAFE) {
        pthread_mutex_destroy(map->__mutex);
        MYFREE(map->__mutex, sizeof(pthread_mutex_t));
    }

    // At last, release the map
    MYFREE(map, sizeof(tmap));
}


void tadd(tmap* map, void* key, void* value) {
    __tSyncWait(map);

    map->__pBufNode = __tget(map, key);

    if(map->__noOverwrite && map->__pBufNode != NULL) {
        fprintf(stderr, "SIGABRT: Key overwrite error: %s\n", (char*)key);
        __tSyncPost(map);
        raise(SIGABRT);
    }

    if(map->__pBufNode != NULL) {
        __tdel(map, key);
    }

    // To simplify, ease reading, use map internal buf variable;
    // next available node:
    map->__pBufNode = &(map->__currentNodeBlock->__nodes[map->__currentNodeBlock->__index]);
    map->__pBufNode->__mynodeblock = map->__currentNodeBlock;
    map->__pBufNode->key = key;
    map->__pBufNode->value = value;
    ++map->__pBufNode->__mynodeblock->__activeNodes;

    tsearch(map->__pBufNode, &map->__root, map->__cmp);

    // increment current node block node index
    ++map->__currentNodeBlock->__index;

    if(map->__currentNodeBlock->__index >= NODE_BLOCK_NB_ELEMENTS) {
        // allocate a new node block
        __nodeBlockAlloc(map);
    }

    __tSyncPost(map);
}


void* tget(tmap* map, void* key) {
    void* v;

    __tSyncWait(map);

    v = map->__pBufNode = __tget(map, key);

    if(v != NULL) {
        v = map->__pBufNode->value;
    }

    __tSyncPost(map);

    return v;
}
