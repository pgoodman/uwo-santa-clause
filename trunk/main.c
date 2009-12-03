/*
 * main.c
 *
 *  Created on: Dec 1, 2009
 *      Author: petergoodman
 *     Version: $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <string.h>

#include "sem.h"
#include "set.h"

#define MAX_WAIT_TIME (INT_MAX >> 4)
#define NUM_REINDEER 10
#define NUM_ELVES 9
#define NUM_ELVES_PER_GROUP 3
#define MAX_MESSAGE_LENGTH 100

/* the sets of semaphores used for the solution.
 *
 * The first is for a few miscellaneous semaphores needed to see if santa is
 * busy, keep mutual exclusion over the reindeer counter, etc.
 *
 * The second has one semaphore per elf. Each semaphore is a mutex lock saying
 * whether or not santa is available to help the elf or not.
 *
 * These sets must be global so that we can ensure they will be freed at exit.
 */
static sem_set_t sem_set;
static sem_set_t elf_line_set;

static sem_t id_counter_mutex;
static sem_t santa_busy_mutex;
static sem_t reindeer_counting_sem;
static set_t elves_waiting;
static sem_t christmas_mutex;
static sem_t elf_counting_sem;
static sem_t elf_mutex;

static sem_t reindeer_counter_lock;
static int num_reindeer_waiting = 0; /* locked by reindeer_counter_lock */

static sem_t elf_counter_lock;
static int num_elves_being_helped = 0;

/**
 * ----------------------------------------------------------------------------
 * General
 * ----------------------------------------------------------------------------
 */

/**
 * Busy wait for an arbitrary amount of time. Before waiting, print out a
 * message to standard output. The message must contain one integer formatting
 * variable. If the message does not contain an integer formatting variable
 * then the integer passed should be -1.
 *
 * Params: - Message to print
 *         - Integer to substitute into the message
 */
static void random_wait(const char *message, const int format_var) {
    unsigned int i = rand() % MAX_WAIT_TIME;
    char formatted_message[MAX_MESSAGE_LENGTH];

    strncpy(&(formatted_message[0]), message, MAX_MESSAGE_LENGTH);
    if(-1 != format_var) {
        snprintf(formatted_message, MAX_MESSAGE_LENGTH, message, format_var);
    }

    fprintf(stdout, "%s", formatted_message);
    for(; --i; ) /* ho ho ho! */;
}

/**
 * Get the current value of a counter and then increment it. Note: this enforces
 * mutual exclusion, thus if there are two distinct counters then incrementing
 * one counter will be mutually exclusive with another. While this isn't
 * desirable, it has a negligible affect on this solution, and is really not
 * the focus of the problem at hand.
 *
 * Params: Pointer to the counter
 */
static int get_value_and_increment(int *counter) {
    int id;

    /* lock the counter so we can get a unique id */
    CRITICAL(id_counter_mutex, {
        id = *counter;
        ++(*counter);
    });

    return id;
}

/**
 * ----------------------------------------------------------------------------
 * Santa-specific
 * ----------------------------------------------------------------------------
 */

/**
 * Have santa help the elves.
 */
static void help_elves(void) {

    int i;
    int elf;

    fprintf(stdout, "Santa: noticed that there are elves waiting! \n");

    sem_wait(santa_busy_mutex);
    CRITICAL(elf_counter_lock, {
        num_elves_being_helped = NUM_ELVES_PER_GROUP;
    });

    /* help the elves */
    CRITICAL(elf_mutex, {
        for(i = i; i < NUM_ELVES_PER_GROUP; ++i) {
            elf = set_take(elves_waiting);
            fprintf(stdout, "Santa: helping elf: %d. \n", elf);
            sem_signal_index(&elf_line_set, elf, 1);
        }
    });
}

/**
 * Prepare the sleigh for the reindeer.
 */
static void prepare_sleigh(void) {

    /* block out elves from getting santa */
    sem_wait(santa_busy_mutex);

    fprintf(stdout, "Santa: preparing the sleigh. \n");

    /* tell the reindeer that they are ready to be hitched. */
    sem_signal_ntimes(reindeer_counting_sem, NUM_REINDEER);
}

/**
 * Run santa's thread.
 */
static void *santa(void *_) {
    int i = INT_MAX;

    /* make sure we eventually terminate. */
    for(; i--; ) {

        /* wait until santa isn't busy to continue */
        CRITICAL(santa_busy_mutex, {
            random_wait("Santa: zzZZzZzzzZZzzz (sleeping) \n", -1);
        });

        /* if we have enough reindeer to deliver presents. Note: while it
         * shouldn't be the case that we have too many reindeer, we account
         * for this possibility and fix the counter. */
        if(NUM_REINDEER <= num_reindeer_waiting) {

            num_reindeer_waiting = NUM_REINDEER;
            prepare_sleigh();

            /* wait until christmas is signalled to be over; christmas starts
             * out as locked, so unlocking christmas intuitively lets it happen.
             * this also forces santa not to fall asleep while the reindeer
             * and being hitched / during christmas. */
            sem_wait(christmas_mutex);

        } else if(3 <= set_cardinality(elves_waiting)) {
            help_elves();
        }
    }

    return NULL;
}

/**
 * ----------------------------------------------------------------------------
 * Elf-specific
 * ----------------------------------------------------------------------------
 */

/**
 * Get help from santa.
 */
static void get_help(const int id) {
    fprintf(stdout, "Elf %d got santa's help! \n", id);
    CRITICAL(elf_counter_lock, {
        --num_elves_being_helped;

        /* unlock santa */
        if(!num_elves_being_helped) {
            sem_signal(santa_busy_mutex);
            sem_signal_ntimes(elf_counting_sem, NUM_ELVES_PER_GROUP);
        }
    });
}

static void *elf(void *_) {

    static int atomic_counter = 0;
    const int id = get_value_and_increment(&atomic_counter);

    while(1) {
        random_wait("Elf %d is working... \n", id);
        fprintf(stdout, "Elf %d needs Santa's help. \n", id);

        /* we need to make sure that if there are three elves waiting that we
         * don't go into the waiting line until those three elves are done. */
        sem_wait(elf_counting_sem);

        CRITICAL(elf_mutex, {
            set_insert(elves_waiting, id);
            fprintf(stdout, "Elf %d in line for santa's help. \n", id);
            sem_wait_index(&elf_line_set, id);
        });

        get_help(id);
    }

    return NULL;
}

/**
 * ----------------------------------------------------------------------------
 * Reindeer-specific
 * ----------------------------------------------------------------------------
 */

static void get_hitched(const int id) {
    fprintf(stdout, "Reindeer %d is getting hitched to the sleigh! \n", id);
}

static void *reindeer(void *_) {

    static int atomic_counter = 0;
    const int id = get_value_and_increment(&atomic_counter);

    /* have the reindeer go on vacation for an arbitrary amount of time and
     * then come back and wait for the other reindeer to return. */
    random_wait("Reindeer %d is on vacation... \n", id);

    CRITICAL(reindeer_counter_lock, {
        ++num_reindeer_waiting;
    });

    /* wait until santa has prepared the sleigh */
    fprintf(stdout,
        "Reindeer %d waiting for the others to arrive back from vacation.\n",
        id
    );

    sem_wait(reindeer_counting_sem);
    CRITICAL(reindeer_counter_lock, {
        get_hitched(id);
        --(num_reindeer_waiting);

        /* all reindeer have been hitched, christmas time! */
        if(0 == num_reindeer_waiting) {
            fprintf(stdout, "Ho ho ho! Off to deliver presents! \n");
            exit(EXIT_SUCCESS);
        }
    });

    return NULL;
}

/**
 * Launch the threads.
 */
static void launch_threads(void) {

    pthread_t thread_ids[1 + NUM_ELVES + NUM_REINDEER];
    pthread_t *curr_id = &(thread_ids[0]);
    int j; /* counter for limiting the number of threads launched */
    void *_ = NULL; /* a value we don't care about */

    /* start up santa, the elves, and the reindeer threads */
    pthread_create(curr_id++, _, &santa, _);

    for(j = NUM_ELVES; j--; ) {
        pthread_create(curr_id++, _, &elf, _);
    }

    for(j = NUM_REINDEER; j--; ) {
        pthread_create(curr_id++, _, &reindeer, _);
    }

    /* join all the threads, i.e. wait for all of them to finish */
    for(--curr_id; curr_id >= &(thread_ids[0]); --curr_id) {
        pthread_join(*curr_id, &_);
    }
}

/**
 * Free all resources.
 */
static void free_resources(void) {
    static int resources_freed = 0;

    if(!resources_freed) {
        resources_freed = 1;
        printf("\nFreeing semaphores.\n");

        sem_empty_set(&sem_set);
        sem_empty_set(&elf_line_set);

        set_exit_free(elves_waiting);
    }
}

/**
 * Handle a SIGINT signal.
 */
static void sigint_handler(int _) {
    exit(EXIT_SUCCESS);
}

/**
 * Simulate the Santa Claus Problem.
 */
int main(void) {

    sem_fill_set(&sem_set, 8);
    sem_fill_set(&elf_line_set, NUM_ELVES);

    elves_waiting = set_alloc(NUM_ELVES);

    if(!atexit(&free_resources)) {
        signal(SIGINT, &sigint_handler);

        /* identify the individual semaphores within the set and then
         * initialize them. */

        sem_unpack_set(&sem_set,
            &id_counter_mutex,
            &reindeer_counter_lock,
            &elf_counter_lock,
            &santa_busy_mutex,
            &reindeer_counting_sem,
            &elf_counting_sem,
            &christmas_mutex,
            &elf_mutex
        );

        sem_init(elf_mutex, 1);
        sem_init(id_counter_mutex, 1);
        sem_init(reindeer_counter_lock, 1);
        sem_init(elf_counter_lock, 1);
        sem_init(santa_busy_mutex, 1);
        sem_init(christmas_mutex, 0); /* starts locked! */
        sem_init(reindeer_counting_sem, 0);
        sem_init(elf_counting_sem, NUM_ELVES_PER_GROUP);

        /* initialize all elf semaphores as mutexes that start off locked! */
        sem_init_all(&elf_line_set, 0);

        /* pseudo-random numbers are used for determining how long elves and
         * reindeer are working and on vacation respectively. */
        srand((unsigned int) time(NULL));

        launch_threads();

    } else {
        fprintf(stderr, "Unable to register atexit handler.\n");
        free_resources();
    }

    set_free(elves_waiting);

    return 0;
}

