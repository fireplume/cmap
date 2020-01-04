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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "linkedList.h"


typedef struct Data {
    // LLNode must be the first element
    LLNode node;
    int index;
    char str[32];
} Data;


int compareInt(const void* a, const void* b) {
    if(((Data*)a)->index < ((Data*)b)->index) {
        return -1;
    } else if(((Data*)a)->index > ((Data*)b)->index) {
        return 1;
    }
    return 0;
}


int compareStr(const void* a, const void* b) {
    return strcmp(((Data*)a)->str, ((Data*)b)->str);
}


int main(int argc, char* argv[]) {
    Data d[] = {
        {.index = 0,    .str = "z"},
        {.index = 26,   .str = "a"},
        {.index = 1,    .str = "y"},
        {.index = 25,   .str = "b"},
        {.index = 2,    .str = "x"},
        {.index = 24,   .str = "c"}
    };

    printf("Creating a list sorted numerically\n");
    LinkedList* list = malloc(sizeof(LinkedList));
    ll_init(list, compareInt);

    for(int i=0; i<sizeof(d)/sizeof(Data); i++) {
        ll_insert(list, (LLNode*)&d[i]);
    }

    Data* node;
    ll_reset_iterator(list);
    while( (node = (Data*)ll_iter(list)) != NULL ) {
        printf("Node: %-3d %s\n", node->index, node->str);
    }

    /////////////////////////////////////////////////////
    printf("\nCreating a list sorted alphabetically\n");
    LinkedList* list2 = malloc(sizeof(LinkedList));
    ll_init(list2, compareStr);

    for(int i=0; i<sizeof(d)/sizeof(Data); i++) {
        ll_insert(list2, (LLNode*)&d[i]);
    }

    ll_reset_iterator(list2);
    while( (node = (Data*)ll_iter(list2)) != NULL ) {
        printf("Node: %-3d %s\n", node->index, node->str);
    }

    free(list);
    free(list2);

    return 0;
}
