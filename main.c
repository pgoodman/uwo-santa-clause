/*
 * main.c
 *
 *  Created on: Dec 1, 2009
 *      Author: petergoodman
 *     Version: $Id$
 *
 * Solution to the Santa Claus Problem, as seen in The Little Book of
 * Semaphores.
 *
 * I think this solution to the Santa Claus problem is deadlock-free. The main
 * reason I think that this solution is deadlock free is because of how the
 * semaphores are used. Most of the semaphores are not actually used to gain
 * mutual exclusion over a shared resource but instead as a means to signal
 * to another thread that something can be done. Thus, there are very few
 * critical sections, where a mutex locks the critical region of memory. As a
 * result, semaphores are left locked or unlocked on purpose, waiting for
 * something to dispatch to them. Further, another goal was to have small
 * critical regions that don't involve more than one mutex over a shared
 * resource (aside from any used in external data structures).
 *
 * Suppose that a deadlock could occur. Most likely, the deadlock would occur
 * with the interaction of one of santa_busy_mutex and santa_sleeping_mutex.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <string.h>

#include "assert.h"
#include "sem.h"
#include "set.h"

#define MAX_WAIT_TIME (INT_MAX >> 4)
#define NUM_REINDEER 10
#define NUM_ELVES 9
#define NUM_ELVES_PER_GROUP 3
#define MAX_MESSAGE_LENGTH 100

#define MAX(a, b) ((a) > (b) ? (a) : (b))

/*
 * NOTE: all global variables below are needed in no fewer than
 *       2 + MIN(NUM_ELVES, NUM_REINDEER) threads, i.e. main, santa, and all
 *       elves or all reindeer. Some of the variables, such as santa_* ones are
 *       needed in every thread. Finally, all sets are needed in the at-exit
 *       handler.
 */

/* set of semaphores used to figure out which elves are currently in line. each
 * elf is given its own semaphore, and in a sense, santa dispatches to the
 * elves that he can help them by signalling particular semaphores in the set.
 * all semaphores in the set start off as locked.
 */
static sem_set_t elf_line_set;

/* set of all semaphores (sem_t) listed below. */
static sem_set_t sem_set;

/* mutexes to keep track of whether or not santa is working with elves or on
 * the sleigh, and whether or not santa is currently asleep. */
static sem_t santa_busy_mutex;
static sem_t santa_sleep_mutex;

/* used to signal when reindeer can start getting hitched, when santa has
 * prepared the sleigh, he signals this counter NUM_REINDEER_TIMES. */
static sem_t reindeer_counting_sem;

/* keep track of how many reindeer are in line, and then how many reindeer have
 * been hitched; locked by reindeer_counter_lock. */
static sem_t reindeer_counter_lock;
static int num_reindeer_waiting = 0;

/* keep track of the elves lined up in an unordered way.  */
static set_t elves_waiting;

/* make sure that no more than NUM_ELVES_PER_GROUP elves line up at one time;
 * starts off at NUM_ELVES_PER_GROUP and then decreases, when santa has helped
 * out the elves it's signalled NUM_ELVES_PER_GROUP times. */
static sem_t elf_counting_sem;

/* make sure that santa helping an elf is mutually exclusive from an elf
 * getting in line to see santa. */
static sem_t elf_mutex;

/* keep track of how many of the NUM_ELVES_PER_GROUP lined up elves have been
 * helped by santa; locked by elf_counter_lock. */
static sem_t elf_counter_lock;
static int num_elves_being_helped = 0;

/**
 * Busy wait for an arbitrary amount of time. Before waiting, print out a
 * message to standard output. The message must contain one integer formatting
 * variable.
 *
 * Params: - Message to print
 *         - Integer to substitute into the message
 */
static void random_wait(const char *message, const int format_var) {
    unsigned int i = rand() % MAX_WAIT_TIME;
    fprintf(stdout, message, format_var);
    for(; --i; ) /* ho ho ho! */;
}

/**
 * ----------------------------------------------------------------------------
 * Santa-specific
 * ----------------------------------------------------------------------------
 */

/**
 * Have santa help the elves; function required in problem specifications.
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

        fprintf(stdout,
            "Santa: There are %d elves outside my door! \n",
            set_cardinality(elves_waiting)
        );

        for(i = 0; i < NUM_ELVES_PER_GROUP; ++i) {
            elf = set_take(elves_waiting);
            fprintf(stdout, "Santa: helping elf: %d. \n", elf);
            sem_signal_index(&elf_line_set, elf, 1);
        }
    });
}

/**
 * Prepare the sleigh for the reindeer; function required by problem
 * specification. Make santa busy (thus blocking elves and sleep) and signal
 * that reindeer can start hitching onto the sleigh.
 */
static void prepare_sleigh(void) {
    sem_wait(santa_busy_mutex);
    fprintf(stdout, "Santa: preparing the sleigh. \n");
    sem_signal_ntimes(reindeer_counting_sem, NUM_REINDEER);
}

/**
 * Santa thread. Note: do not launch more than one!
 */
static void *santa(void *_) {
    static int num_launched = 0;

    assert(1 == ++num_launched);

    while(1) {

        /* wait until santa isn't busy to continue */
        CRITICAL(santa_busy_mutex, {
            fprintf(stdout, "Santa: zzZZzZzzzZZzzz (sleeping) \n");
        });

        sem_wait(santa_sleep_mutex);

        fprintf(stdout, "Santa: I'm up, I'm up! Whaddya want? \n");

        if(NUM_REINDEER <= num_reindeer_waiting) {

            num_reindeer_waiting = NUM_REINDEER;
            prepare_sleigh();

            /* completely lock santa; It's time to deliver presents! */
            sem_wait(santa_busy_mutex);
            sem_wait(santa_sleep_mutex);

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
 * Get help from santa; function required in problem specifications.
 */
static void get_help(const int id) {
    fprintf(stdout, "Elf %d got santa's help! \n", id);

    CRITICAL(elf_counter_lock, {
        --num_elves_being_helped;

        /* unlock santa; signal that elves can line up again */
        if(!num_elves_being_helped) {
            sem_signal(santa_busy_mutex);
            sem_signal_ntimes(elf_counting_sem, NUM_ELVES_PER_GROUP);
        }
    });
}

/**
 * A single elf thread.
 */
static void *elf(void *elf_id) {
    const int id = *((int *) elf_id);

    while(1) {
        random_wait("Elf %d is working... \n", id);
        fprintf(stdout, "Elf %d needs Santa's help. \n", id);

        /* we need to make sure that if there are three elves waiting that we
         * don't go into the waiting line until those three elves are done. */
        sem_wait(elf_counting_sem);

        CRITICAL(elf_mutex, {
            set_insert(elves_waiting, id);
            fprintf(stdout, "Elf %d in line for santa's help. \n", id);

            /* wake up santa */
            if(NUM_ELVES_PER_GROUP == set_cardinality(elves_waiting)) {
                fprintf(stdout, "Elves: waking up santa! \n");
                sem_signal(santa_sleep_mutex);
            }
        });

        sem_wait_index(&elf_line_set, id);
        get_help(id);
    }

    return NULL;
}

/**
 * ----------------------------------------------------------------------------
 * Reindeer-specific
 * ----------------------------------------------------------------------------
 */

/**
 * Have a reindeer get hitched; function required by problem specifications.
 */
static void get_hitched(const int id) {
    fprintf(stdout, "Reindeer %d is getting hitched to the sleigh! \n", id);
}

/**
 * A single reindeer thread.
 */
static void *reindeer(void *reindeer_id) {
    const int id = *((int *) reindeer_id);

    /* have the reindeer go on vacation for an arbitrary amount of time and
     * then come back and wait for the other reindeer to return. */
    random_wait("Reindeer %d is off to the Tropics! \n", id);

    CRITICAL(reindeer_counter_lock, {
        ++num_reindeer_waiting;
    });

    fprintf(stdout, "Reindeer %d is back from the Tropics.\n", id);

    if(NUM_REINDEER <= num_reindeer_waiting) {
        fprintf(stdout, "Reindeer %d: I'm the last one; I'll get santa!\n", id);
        sem_signal(santa_sleep_mutex);
    }

    /* santa is awake, now wait for him to tell us to get hitched */
    sem_wait(reindeer_counting_sem);

    /* the sleigh has been prepared, time to get hitched and go! */
    CRITICAL(reindeer_counter_lock, {

        get_hitched(id);
        --(num_reindeer_waiting);

        /* all reindeer have been hitched, christmas time! */
        if(0 == num_reindeer_waiting) {
            fprintf(stdout, "Santa: Ho ho ho! Off to deliver presents! \n");
            exit(EXIT_SUCCESS);
        }
    });

    return NULL;
}

/**
 * ----------------------------------------------------------------------------
 * Set up and run the problem.
 * ----------------------------------------------------------------------------
 */

/**
 * Launch threads in sequence over some arrays of thread ids and arguments.
 */
static void sequence_pthreads(int num_threads,
                              pthread_t *thread_ids,
                              void *(*func)(void *),
                              int *args) {
    int i;
    for(i = 0; num_threads--; ++i) {
        pthread_create(&(thread_ids[i]), NULL, func, (void *) &(args[i]));
    }
}

/**
 * Free all resources. Note: performing a set_free as opposed to a
 * set_exit_free would result (usually) in an error calling free().
 */
static void free_resources(void) {
    static int resources_freed = 0;
    if(!resources_freed) {
        resources_freed = 1;
        fprintf(stdout,"\n... And that year was a Merry Christmas indeed!\n\n");
        sem_empty_set(&sem_set);
        sem_empty_set(&elf_line_set);
        set_exit_free(elves_waiting);
    }
}

/**
 * Handle a SIGINT signal; make it call the at-exit handler.
 */
static void sigint_handler(int _) {
    exit(EXIT_SUCCESS);
}

/**
 * Launch the threads.
 */
static void launch_threads(void) {

    pthread_t thread_ids[1 + NUM_ELVES + NUM_REINDEER];

    int ids[MAX(NUM_ELVES, NUM_REINDEER)];
    int i; /* index into the ids */

    /* fill up the ids */
    for(i = 0; i < MAX(NUM_ELVES, NUM_REINDEER); ++i) {
        ids[i] = i;
    }

    /* start up santa, the elves, and the reindeer threads */
    pthread_create(&(thread_ids[0]), NULL, &santa, NULL);
    sequence_pthreads(NUM_ELVES, &(thread_ids[1]), &elf, &(ids[0]));
    sequence_pthreads(NUM_REINDEER, thread_ids + 1 + NUM_ELVES, &reindeer, ids);

    /* necessary to wait instead of pthread_exit, otherwise stack, and so
     * values pointed at by ids and thread_ids get corrupted. */
    for(i = 0; i < (1 + NUM_ELVES + NUM_REINDEER); ++i) {
        pthread_join(thread_ids[i], NULL);
    }
}

/**
 * Simulate the Santa Claus Problem.
 */
int main(void) {

    sem_fill_set(&sem_set, 7);
    sem_fill_set(&elf_line_set, NUM_ELVES);

    elves_waiting = set_alloc(NUM_ELVES);

    if(!atexit(&free_resources)) {
        signal(SIGINT, &sigint_handler);

        /* identify the individual semaphores within the set and then
         * initialize them. */

        sem_unpack_set(&sem_set,
            &reindeer_counter_lock,
            &elf_counter_lock,
            &santa_busy_mutex,
            &santa_sleep_mutex,
            &reindeer_counting_sem,
            &elf_counting_sem,
            &elf_mutex
        );

        sem_init(elf_mutex, 1);
        sem_init(reindeer_counter_lock, 1);
        sem_init(elf_counter_lock, 1);
        sem_init(santa_busy_mutex, 1);
        sem_init(santa_sleep_mutex, 0); /* starts as locked! */
        sem_init(reindeer_counting_sem, 0);
        sem_init(elf_counting_sem, NUM_ELVES_PER_GROUP);

        /* initialize all elf semaphores as mutexes that start off *locked* */
        sem_init_all(&elf_line_set, 0);

        /* pseudo-random numbers are used for making random-length busy waits.*/
        srand((unsigned int) time(NULL));

        launch_threads();

    } else {
        fprintf(stderr, "Unable to register an at-exit handler.\n");
        free_resources();
    }

    set_free(elves_waiting);

    return 0;
}

