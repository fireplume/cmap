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


/*

As for the <search.h> implementation, you are responsible for managing the memory
of given keys and values.

Whereas it doesn't matter this implementation as far as values go, if you release
memory for a key while it is being used, your program will likely crash or behave
in unexpected ways.

*/

#ifndef TMAP_H
#define TMAP_H
#include <search.h>
#include <pthread.h>
#include <semaphore.h>

#define TMAP_NO_OVERWRITE 1
#define TMAP_ALLOW_OVERWRITE 0

#define SINGLE_THREADED 0
#define MULTI_THREAD_SAFE 1


// Structure for client who wants to provide their own allocator
// to tmap.
typedef struct tallocator {
    void* (*tmyalloc)(size_t);
    void  (*tmyfree)(void*, size_t);
} tallocator;


// Internal node structure containing client's map data
typedef struct tnode {
    void* key;
    void* value;
} tnode;


// Internal structure for memory management
typedef struct tnodeblock tnodeblock;
typedef struct tnodeblock {
    // Next block of nodes
    tnodeblock* __next;
    // Array of nodes available for client data allocation
    tnode* __nodes;
    // Index of next available node
    unsigned int __index;
} tnodeblock;


typedef struct tmap {
    // binary tree root, used directly by <search.h>
    void* __root;

    // Key compare function pointer
    int (*__cmp)(const void*, const void*);

    // accelerator members: TODO: Keep? Any diff in performance?
    tnode* __pBufNode;

    // Overwrite permission flag
    int __noOverwrite;

    // Memory management
    tnodeblock* __firstNodeBlock;
    tnodeblock* __currentNodeBlock;

    // Multi thread flag
    int __multitask;
    pthread_mutex_t* __mutex;
} tmap;


/*****************************************************************/
// Following are not multithread safe
/*****************************************************************/

// Override memory allocator
extern void tconf(tallocator* allocator);

// Obtain an instance of tmap
extern tmap* tinit(int (*cmp)(const void*, const void*),
                   const int noOverwrite,
                   const int multitask);

// Override key comparison function
extern void tsetcmp(tmap* map,
                    int (*cmp)(const void*, const void*));

// Free memory for given map object
extern void tfree(tmap* map);

/*****************************************************************/
// Following are multithread safe
/*****************************************************************/

// Get binary tree root for use in 'twalk'
extern void* troot(tmap* map);

// Add a key to the binary tree
extern void tadd(tmap* map, void* key, void* value);

// Delete a key from the binary tree
extern void tdel(tmap* map, void* key);

// Get value of a key
extern void* tget(tmap* map, void* key);

#endif
