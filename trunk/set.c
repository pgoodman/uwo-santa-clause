/*
 * set.c
 *
 *  Created on: Dec 1, 2009
 *      Author: petergoodman
 *     Version: $Id$
 *
 * Library implementing a bit (well, char) set using locks. This is a fairly
 * nice data structure as a given item will only ever go into a specific slot
 * in the set. An item can be put in as many times without changing the state
 * of the set. Thus, two threads won't ever clobber the state of the set.
 */

#include "set.h"

/**
 * This is equivalent to a bitset; however, I was a bit lazy, so I just made it
 * into a char set because that takes less time to make.
 */
struct set {
    sem_t write_lock;
    sem_set_t semaphore_set;
    char *slots;

    int num_slots;
    int cardinality;
};

/**
 * Allocate a new fixed-size set.
 *
 * Params: - The size of the set.
 */
set_t set_alloc(const int num_slots) {

    set_t set = NULL;
    size_t obj_size = sizeof(struct set);
    size_t buff_size = sizeof(char) * num_slots;
    size_t inv_pad;

    assert(0 < num_slots);

    /* make sure that the set's slots are 8-byte aligned */
    obj_size += (inv_pad = obj_size % 8) > 0 ? 8 - inv_pad : 0;
    set = (set_t) malloc(obj_size + buff_size);
    if(NULL == set) {
        return NULL;
    }

    set->slots = ((char *) set) + obj_size;
    set->cardinality = 0;
    set->num_slots = num_slots;


    memset(&(set->slots[0]), 0, buff_size);

    /* get the write lock */
    sem_fill_set(&(set->semaphore_set), 1);
    sem_unpack_set(&(set->semaphore_set), &(set->write_lock));
    sem_init(set->write_lock, 1);

    return set;
}

/**
 * Free the semaphores at exit. This should only be called within an atexit
 * handler. This does not actually free the heap object.
 */
void set_exit_free(set_t set) {
    assert(NULL != set);
    sem_empty_set(&(set->semaphore_set));
}

/**
 * Free the set.
 *
 * Params: - Pointer to the set to be freed.
 */
void set_free(set_t set) {
    assert(NULL != set);
    set_exit_free(set);
    free(set);
}

/**
 * Add an item (integer) into the set.
 *
 * Params: - Pointer to the set to add an item to.
 */
void set_insert(set_t set, const int item) {
    assert(NULL != set);
    assert(item >= 0 && item < set->num_slots);

    CRITICAL(set->write_lock, {
        /* add the item into the set */
        set->slots[item] = 1;
        if(set->cardinality < set->num_slots) {
            ++(set->cardinality);
        }
    });
}

/**
 * Remove a random item (integer) from the set.
 *
 * Params: - Pointer to the set to remove an item from.
 */
int set_take(set_t set) {
    int item;
    assert(NULL != set);

    CRITICAL(set->write_lock, {
        /* find a random item in the set */
        for(item = rand() % set->num_slots;
            !set->slots[item];
            item = rand() % set->num_slots);

        set->slots[item] = 0;
        --(set->cardinality);
    });

    return item;
}

/**
 * Get the number of items currently in the set.
 *
 * Params: - Pointer to the set being queried.
 */
int set_cardinality(const set_t set) {
    return set->cardinality;
}
