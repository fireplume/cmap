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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "myalloc.h"


void allocate_init_free(int nb_alloc, size_t size) {
	register int i;
	register int j;

	// -1 to take into account array malloc
	--nb_alloc;

	// Allocate
	int** array = (int**)malloc(nb_alloc);
	for(int i=0; i<nb_alloc; i++) {
	    array[i] = malloc(size*sizeof(int));
	    memset(array[i], i%256, size*sizeof(int));
	}

	// Verify
	int v;
	for(i=0; i<nb_alloc; i++) {
		for(j=0;j<size; j++) {
		    v = (i%256) | (i%256) << 8 | (i%256) << 16 | (i%256) << 24;
			if(array[i][j] != v) {
				fprintf(stderr, "ERROR: array[%d][%d] == %d != %d\n", i, j, array[i][j], v);
			}
		}
	}

	// free
    for(int i=0; i<nb_alloc; i++) {
        free(array[i]);
    }
    free(array);
}


int main(int argc, char* argv[]) {
	void* ptr[11];
    int c;
    int iterations = 1;
    int nb_alloc = 8;
    size_t alloc_size = 4096;

    while ((c = getopt (argc, argv, "n:i:s:")) != -1) {
        switch (c)
        {
			case 'n':
				nb_alloc = atoi(optarg);
				break;
			case 'i':
				iterations = atoi(optarg);
				break;
            case 's':
                alloc_size = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Bad argument: %c\n", c);
        }
    }

    while(iterations--) {
    	allocate_init_free(nb_alloc, alloc_size);
    }

	return 0;
}
