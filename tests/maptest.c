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

#include <errno.h>
#include <getopt.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "allocSample.h"
#include "multitaskMapTest.h"
#include "tmap.h"
#include "myalloc.h"


#define MAX_KEY_SIZE 16


/*********************************************************************************
 * Necessary misc functions, look at tsearch doc for details
 *********************************************************************************/

// For string keys, this is the compare function you shall define (strcmp)
static int
compare(const void* pa, const void* pb)
{
    return strcmp( (char*)(((tnode*)pa)->key), (char*)(((tnode*)pb)->key) );
}

// This is a sample action function to walk/print all key/value elements
// for a map of string to string
static void
action(const void *nodep, VISIT which, int depth)
{
    tnode *datap;

    switch (which) {
        case preorder:
            break;
        case postorder:
            datap = (*(tnode **) nodep);
            printf("      %s -> %s\n", (char*)datap->key, (char*)datap->value);
            break;
        case endorder:
            break;
        case leaf:
            datap = (*(tnode **) nodep);
            printf("      %s -> %s\n", (char*)datap->key, (char*)datap->value);
            break;
    }
}


// Function to print the whole map and an example usage of "twalk"
void printMap(tmap* map) {
    printf("   map:\n");
    twalk(troot(map), action);
}


/* For Key/value overwrite test and out of memory case*/
jmp_buf jmpBuf;
struct sigaction signalHandlerObj;
void signalHandler(int signo) {
    puts("DEBUG: Caught signal");
    if(signo == SIGABRT) {
        longjmp(jmpBuf, 1);
    }
}


/*********************************************************************************
 * Tests, sample usage
 *********************************************************************************/


int check(tmap* map, char* key, char* expected) {
    char* value = (char*)tget(map, key);
    printf("   Checking %-16s Expected: %-25s", key, value);
    if( (value && expected && strcmp(expected, value)) || (value != expected) ) {
        printf("ERROR: Test failed: got %s\n", value);
        return 1;
    }
    printf("PASS!\n");
    return 0;
}


void mapPerformanceTest(const int nbElements,
                        const int mapMultiTaskSupport,
                        const int customAllocator) {
    tallocator* allocator = NULL;
    if(customAllocator) {
        allocator = malloc(sizeof(tallocator));
        allocator->tmyalloc = myalloc;
        allocator->tmyfree = myfree;

        // Initialize map engine with allocator
        tconf(allocator);
    }

    tmap* map = tinit(compare, TMAP_ALLOW_OVERWRITE, mapMultiTaskSupport);

    // Allocating a big chunk and managing sub pointers manually as for
    // big number of elements (>60000), we hit the system mappings limit
    // and get ENOMEM (when compiled with MULTIPROC_SUPPORT)
    char** buf;
    buf = (char**)malloc(nbElements*sizeof(char*)+MAX_KEY_SIZE*nbElements);
    if(buf == NULL) {
        fprintf(stderr, "mapPerformanceTest: Allocation failed: %s\n", strerror(errno));
        exit(-1);
    }

    int baseOffset = nbElements*sizeof(char*);
    for(int i=0; i<nbElements; i++) {
        buf[i] = (char*)buf + baseOffset + i * MAX_KEY_SIZE;
        snprintf(buf[i], MAX_KEY_SIZE, "%06d", i*2);
    }

    // Initialization time measurement
    clock_t tClock = clock();
    for(int i=0; i<nbElements; i++) {
        tadd(map, buf[i], buf[i]);
        tdel(map, buf[i]);
        tadd(map, buf[i], buf[i]);
    }
    tClock = clock() - tClock;
    fprintf(stderr, "[%-5d] Map init time: %-3.2f seconds\n", nbElements, (float)tClock/CLOCKS_PER_SEC);

    // Access time measurement
    tClock = clock();
    for(int i=0; i<nbElements; i++) {
        if(tget(map, buf[i]) == NULL) {
            printf("Failed on key at index: %d\n", i);
        }
    }
    tClock = clock() - tClock;
    fprintf(stderr, "[%-5d] Access time:   %-3.2f seconds\n", nbElements, (float)tClock/CLOCKS_PER_SEC);

    // cleanup
    tClock = clock();
    tfree(map);
    tClock = clock() - tClock;
    fprintf(stderr, "[%-5d] Map memory release time:   %-3.2f seconds\n", nbElements, (float)tClock/CLOCKS_PER_SEC);

    free(buf);

    if(customAllocator) {
        free(allocator);
    }

    // No crash is a pass
    fprintf(stderr, "PASS!\n");
}


void overwriteTest(const int mapMultiTaskSupport) {
    tmap* map;
    int errors = 0;

    printf("---------------------------------------------------------\n");
    printf("Test key overwrite triggers signal with TMAP_NO_OVERWRITE\n");
    map = tinit(compare, TMAP_NO_OVERWRITE, mapMultiTaskSupport);

    // setup signal handler
    signalHandlerObj.sa_handler = signalHandler;
    sigemptyset(&(signalHandlerObj.sa_mask));
    sigaddset(&(signalHandlerObj.sa_mask), SIGABRT);
    signalHandlerObj.sa_flags = 0 ;
    sigaction(SIGABRT, &signalHandlerObj, NULL);

    tadd(map, "keyA", (void*)"untouched");
    int r = setjmp(jmpBuf);
    if(r==0) {
        tadd(map, "keyA", (void*)"Key overwritten");
    }
    printMap(map);
    errors += check(map, "keyA", "untouched");
    tfree(map);

    signalHandlerObj.sa_handler = SIG_DFL;
    sigaction(SIGABRT, &signalHandlerObj, NULL);

    // Rationale for this test: when adding duplicate key to tsearch binary tree,
    // getting back the value for this key is undertermined (can be any).
    // When adding a duplicate key, tmap erases the original one, avoiding
    // the issue altogether.
    printf("---------------------------------------------------------\n");
    printf("Test key overwrite returns proper value\n");
    map = tinit(compare, TMAP_ALLOW_OVERWRITE, mapMultiTaskSupport);

    tadd(map, "keyA", (void*)"Original keyA");
    errors += check(map, "keyA", "Original keyA");

    tadd(map, "keyA", (void*)"KeyA overwritten once");
    errors += check(map, "keyA", "KeyA overwritten once");

    tadd(map, "keyA", (void*)"KeyA overwritten twice");
    errors += check(map, "keyA", "KeyA overwritten twice");

    tfree(map);

    if(errors == 0) {
        fprintf(stderr, "PASS!\n");
    } else {
        fprintf(stderr, "FAIL!\n");
    }
}


void basicAccessorTest(const int mapMultiTaskSupport) {
    tmap* map;
    int errors = 0;

    map = tinit(compare, TMAP_ALLOW_OVERWRITE, mapMultiTaskSupport);

    printf("Test basic accessors (tinit, tadd, tdel, tget)");

    printf("TEST: add/del 'ae'\n");
    tadd(map, "ae", (void*)"!ea");
    tdel(map, "ae");
    printMap(map);
    errors += check(map, "ae", NULL);

    printf("TEST: add ae,ac rm ae\n");
    tadd(map, "ae", (void*)"!ea");
    tadd(map, "ac", (void*)"!ca");
    tdel(map, "ae");
    printMap(map);
    errors += check(map, "ac", "!ca");
    errors += check(map, "ae", NULL);

    printf("TEST: add ae rm ac\n");
    tadd(map, "ae", (void*)"!ea");
    tdel(map, "ac");
    printMap(map);
    errors += check(map, "ac", NULL);
    errors += check(map, "ae", "!ea");

    printf("TEST: add ab,ac,ad rm ae\n");
    tadd(map, "ac", (void*)"!ca");
    tadd(map, "ad", (void*)"!da");
    tadd(map, "ab", (void*)"!ba");
    tdel(map, "ae");
    printMap(map);
    errors += check(map, "ae", NULL);
    errors += check(map, "ac", "!ca");
    errors += check(map, "ab", "!ba");
    errors += check(map, "ad", "!da");

    printf("TEST: add ae, rm ac\n");
    tadd(map, "ae", (void*)"!ea");
    tdel(map, "ac");
    printMap(map);
    errors += check(map, "ac", NULL);
    errors += check(map, "ae", "!ea");
    errors += check(map, "ab", "!ba");
    errors += check(map, "ad", "!da");

    printf("TEST: rm ab,ad,ae\n");
    tdel(map, "ab");
    tdel(map, "ad");
    tdel(map, "ae");
    printMap(map);
    errors += check(map, "ab", NULL);
    errors += check(map, "ac", NULL);
    errors += check(map, "ad", NULL);
    errors += check(map, "ae", NULL);

    printf("TEST: rm ae again\n");
    tdel(map, "ae");
    printMap(map);
    errors += check(map, "ae", NULL);

    printf("TEST: never used key\n");
    errors += check(map, "?key?", NULL);

    tfree(map);

    if(errors == 0) {
        fprintf(stderr, "PASS!\n");
    } else {
        fprintf(stderr, "FAIL!\n");
    }
}


void printHelp(char* argv[]) {
    printf("\
Usage: %s [-h] [-t <b|o|p|mt|all>] [-e <nbElements>] [-p <parallel>] [-s] [-m] [-i <iterations>\n\
    -t:\n\
        b:   basic map accessor test\n\
        o:   key/value overwrite test\n\
        p:   performance test\n\
        mt:  multi threaded test\n\
        mm:  custom memory allocator test\n\
        all: run all tests\n\
    -e:\n\
        Performance test: total number of elements to create\n\
        Multi[thread|proc] test: number of elements to create == (int)nbElements/parallel\n\
    -p:\n\
        Number of tasks (threads or processes) to create\n\
    -s:\n\
        Force multithreaded/multiprocess tests to run map with single thread support to cause errors\n\
    -m:\n\
        Run non multithreaded/multiprocess tests with multiprocess support (to test memory allocator)\n\
    -i:\n\
        Iterate 'iterations' number of times over the requested test(s)\n\
\n\
Note:\n\
- You should run the perf test with %s -t p -e nbElements > /dev/null\n\
-s: The test is still run in multithreaded fashion, but the map\n\
    object is configured to work as in single threaded mode, which should cause errors.\n", argv[0], argv[0]);
}


int main(int argc, char* argv[]) {
    int c;
    char test[4];
    unsigned int nbElements = 4000;
    int nbParallelTasks = 10;
    int singleThreadedMode = 0;
    int mapMultiTaskMode = SINGLE_THREADED;
    int iterations = 1;

    if(argc == 1) {
        printHelp(argv);
        exit(0);
    }

    while ((c = getopt (argc, argv, "ht:e:p:si:m")) != -1) {
        switch (c)
        {
            case 't':
                strncpy(test, optarg, 4);
                break;
            case 'h':
                printHelp(argv);
                exit(0);
                break;
            case 'e':
                nbElements = atoi(optarg);
                break;
            case 'i':
                iterations = atoi(optarg);
                break;
            case 'p':
                nbParallelTasks = atoi(optarg);
                break;
            case 's':
                singleThreadedMode = 1;
                break;
            default:
                fprintf(stderr, "Bad argument: %c\n", c);
        }
    }

    printf("===============================\n");
    printf("Run parameters:\n");
    printf("   %-25s%d\n", "Number of elements:", nbElements);
    printf("   %-25s%d\n", "Number of tasks:", nbParallelTasks);
    printf("   %-25s%d\n", "Single threaded mode:", singleThreadedMode);
    printf("===============================\n\n");

    for(int i=1;i<=iterations;i++) {
        fprintf(stderr, "              ++++++++++++++++\n");
        fprintf(stderr, "               ITERATION %d\n", i);
        fprintf(stderr, "              ++++++++++++++++\n\n");

        if(!strcmp(test, "b") || !strcmp(test, "a")) {
            fprintf(stderr, "############## basicAccessorTest ##############\n");
            basicAccessorTest(mapMultiTaskMode);
        }

        // Haven't taken the time to make the signal handler work more than once
        if((iterations == 1) && (!strcmp(test, "o") || !strcmp(test, "a"))) {
            fprintf(stderr, "############## overwriteTest ##############\n");
            overwriteTest(mapMultiTaskMode);
        }

        if(!strcmp(test, "p") || !strcmp(test, "a")) {
            fprintf(stderr, "############## mapPerformanceTest ##############\n");
            mapPerformanceTest(nbElements, mapMultiTaskMode, 0);
        }

        if(!strcmp(test, "p") || !strcmp(test, "a")) {
            fprintf(stderr, "############## mapPerformanceTest ##############\n");
            fprintf(stderr, "############## +custom allocator  ##############\n");
            mapPerformanceTest(nbElements, mapMultiTaskMode, 1);
        }

        if(!strcmp(test, "mt") || !strcmp(test, "a")) {
            int nbElPerThread = nbElements/nbParallelTasks;
            fprintf(stderr, "############## multithreadTest ##############\n");
            printf("Elements/thread: %d\n", nbElPerThread);
            multithreadTest(nbParallelTasks, nbElPerThread, singleThreadedMode);
        }
    }

    return 0;
}
