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

#ifndef UTILS_LINKEDLIST_H_
#define UTILS_LINKEDLIST_H_

#include <signal.h>

// One way to improve speed is for client to pre-allocate blocks of nodes.
// That way, it insures that, within a block of nodes, it is less
// likely to have page/cache miss.


typedef struct LLNode LLNode;
typedef struct LLNode {
    LLNode* __next;
    LLNode* __previous;
} LLNode;


typedef struct LinkedList LinkedList;
typedef struct LinkedList {
    // First element of LinkedList at same address of LinkedList
    LLNode* head;
    LLNode* tail;
    LLNode* last_insert;
    LLNode* __buffer;
    LLNode* __it;
    LLNode* __it_buf;
    int __nb_items;
    int (*__compare)(const void*, const void*);
} LinkedList;


// The __compare function provided to getLinkedList should properly
// cast the (void*) parameters to the proper comparison type.
void ll_init(LinkedList* l, int (*cmp)(const void*, const void*)) {
    l->head = NULL;
    l->tail = NULL;
    l->__it = NULL;
    l->__compare = cmp;
    l->last_insert = NULL;
    l->__nb_items = 0;
}


void ll_insert(LinkedList* list, LLNode* node) __attribute__((always_inline));
inline void ll_insert(LinkedList* list, LLNode* node) {
    list->last_insert = node;
    ++(list->__nb_items);

    if(list->head == NULL) {
       list->head = node;
       list->tail = node;
       node->__next = NULL;
       node->__previous = NULL;
        return;
    }

    // If "__compare" is not set, or node is greater than
    // the last one, simply append
    if((list->__compare == NULL) ||
       (list->__compare(node, list->tail) > 0)) {
        list->__buffer = list->tail;

        // 'node' is our new tail
        list->tail = node;
        list->tail->__next = NULL;
        list->tail->__previous = list->__buffer;

        // Set old tail to point to new tail
        list->__buffer->__next = node;

        return;
    }

    // Is it smaller than HEAD?
    if( list->__compare(node, list->head) < 0 ) {
        list->__buffer = list->head;

        // 'node' is our new HEAD
        list->head = node;
        list->head->__next = list->__buffer;
        list->head->__previous = NULL;

        // Set old head to point to new head
        list->__buffer->__previous = node;

        return;
    }

    // Sort it into the list
    list->__buffer = list->head->__next;
    while(list->__buffer != 0) {
        if(list->__compare(node, list->__buffer) < 0 ) {
            // node fits just before list->__buffer
            node->__previous = list->__buffer->__previous;
            node->__next = list->__buffer;

            list->__buffer->__previous->__next = node;
            list->__buffer->__previous = node;

            return;
        }
        list->__buffer = list->__buffer->__next;
    }

    // Shouldn't be here
    raise(SIGABRT);
}


void ll_del(LinkedList* list, LLNode* node) __attribute__((always_inline));
inline void ll_del(LinkedList* list, LLNode* node) {
    // Ok if in the midst of iteration, as long as
    // client doesn't delete passed current
    // iteration

    if(node == list->head) {
        list->head = node->__next;
    }
    if(node == list->tail) {
        list->tail = node->__previous;
    }
    if(node == list->last_insert) {
        list->last_insert = NULL;
    }


    if(node->__previous != NULL) {
        node->__previous->__next = node->__next;
    }
    if(node->__next != NULL) {
        node->__next->__previous = node->__previous;
    }

    --list->__nb_items;
    if(list->__nb_items < 0) {
        raise(SIGABRT);
    }
}


void ll_reset_iterator(LinkedList* list) {
    list->__it = list->head;
}


LLNode* ll_iter(LinkedList* list) __attribute__((always_inline));
inline LLNode* ll_iter(LinkedList* list) {
    if(list->__it == NULL) {
        list->__it = list->head;
        return NULL;
    }
    list->__it_buf = list->__it;
    list->__it = list->__it->__next;
    return list->__it_buf;
}


int ll_nb_items(LinkedList* list) {
    return list->__nb_items;
}

#endif /* UTILS_LINKEDLIST_H_ */
