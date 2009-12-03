/*
 * sem.c
 *
 *  Created on: Dec 1, 2009
 *      Author: petergoodman
 *     Version: $Id$
 *
 * Library for working with a small set of semaphores. This library allows one
 * to individually work with semaphores with the appearance that they are
 * independent of the set to which they belong. For small problems this is
 * extremely convenient.
 */

#include "sem.h"

/**
 * Types of arguments for semop and semctl.
 */
typedef union {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
} my_semun_t;

typedef struct sembuf my_sembuf_t;

/**
 * Fill a semaphore set. Prints an error if
 *
 * Params: - Pointer to the semaphore set to fill
 *         - The number of semaphores to fill the set with.
 *
 * Side-Effects: If this function fails then the program will be exited.
 */
void sem_fill_set(sem_set_t *set, const int num_semaphores) {

    assert(NULL != set);
    assert(0 < num_semaphores);

    set->num_semaphores = num_semaphores;
    set->id = semget(
        IPC_PRIVATE,
        num_semaphores,
        IPC_CREAT | IPC_EXCL | IPC_NOWAIT | 0666
    );

    if(-1 == set->id) {
        perror("sem_fill_set[semget]");
        exit(EXIT_FAILURE);
    }
}

/**
 * Empty a set of semaphores.
 *
 * Params: - Pointer to a filled semaphore set.
 *
 * Note: emptying a set doesn't change the state of any previously unpacked
 *       semaphores from the set. Using these semaphores after the set has
 *       been emptied results in undefined behavior.
 *
 * Side-Effects: If this function fails then the program will be exited.
 */
void sem_empty_set(sem_set_t *set) {
    my_semun_t _;
    assert(NULL != set);

    if(-1 == semctl(set->id, 0, IPC_RMID, _)) {
        perror("sem_empty_set[semctl]");
        exit(EXIT_FAILURE);
    }

    set->id = -1;
    set->num_semaphores = -1;
}

/**
 * Unpack a semaphore set into individual semaphores that can then be
 * initialized.
 *
 * Params: - Pointer to the semaphore set to unpack
 *         - Variable number of semaphores to unpack from the set; the number
 *           of arguments expected is the same as the number of semaphores in
 *           the set.
 *
 * Side-Effects: If this function fails then the program will be exited.
 */
void sem_unpack_set(sem_set_t *set, sem_t *sem1, ...) {
    va_list sems;
    sem_t *curr_sem;
    int i;

    va_start(sems, sem1);

    assert(NULL != set);
    assert(NULL != sem1);

    for(i = 0, curr_sem = sem1;
        i < set->num_semaphores;
        ++i, curr_sem = va_arg(sems, sem_t *)) {

        curr_sem->num = i;
        curr_sem->set = set;
    }

    va_end(sems);
}

/**
 * Initialize a given semaphore to some value.
 *
 * Params: - Pointer to semaphore set
 *         - Index into the semaphore set of the semaphore we're interested in
 *         - Value to initialize the semaphore
 *
 * Side-Effects: If this function fails then the program will be exited.
 */
void sem_init_index(sem_set_t *set, const int sem_index, const int value) {
    my_semun_t arg;

    assert(NULL != set);
    assert(0 <= sem_index && sem_index < set->num_semaphores);

    arg.val = value;
    if(-1 == semctl(set->id, sem_index, SETVAL, arg)) {
        perror("sem_init_index[semctl]");
        exit(EXIT_FAILURE);
    }
}

/**
 * Initialize all semaphores within a set.
 *
 * Params: - Pointer to semaphore set.
 *         - Value which will be assigned to all semaphores in the above set.
 */
void sem_init_all(sem_set_t *set, const int value) {
    int i;
    unsigned short *sem_ids;
    my_semun_t arg;
    struct semid_ds sem_buf;
    assert(NULL != set);

    arg.buf = &sem_buf;

    /* get the info */
    if(-1 == semctl(set->id, 0, IPC_STAT, arg))  {
        perror("sem_init_all[semctl:IPC_STAT]");
        exit(EXIT_FAILURE);
    }

    /* fill the ids */
    sem_ids = alloca(sizeof(unsigned short) * set->num_semaphores);
    arg.array = &(sem_ids[0]);
    for(i = 0; i < set->num_semaphores; ++i) {
        sem_ids[i] = value;
    }

    /* initialize the values */
    if(-1 == semctl(set->id, 0, SETALL, arg)) {
        perror("sem_init_all[semctl:SETALL]");
        exit(EXIT_FAILURE);
    }
}

/**
 * Signal all semaphores within a set.
 *
 * Params: - Pointer to semaphore set.
 *         - The number of times to signal each semaphore in the set.
 *
 * Note: the current implementation repeatedly calls sem_signal_index, which
 *       isn't ideal; however, it is much simpler than mucking around with
 *       semctl and SETALL.
 */
void sem_signal_all(sem_set_t *set, const int num_signals) {
    int i;
    assert(NULL != set);
    for(i = 0; i < set->num_semaphores; ++i) {
        sem_signal_index(set, i, num_signals);
    }
}

/**
 * Wait until a given semaphore has cleared.
 *
 * Params: - Pointer to semaphore set to which the indexed semaphore belongs.
 *         - Index of semaphore to wait on.
 *
 * Side-Effects: If this function fails then the program will be exited.
 */
void sem_wait_index(sem_set_t *set, const int sem_index) {
    my_sembuf_t op;

    assert(NULL != set);
    assert(0 <= sem_index && sem_index < set->num_semaphores);

    op.sem_num = sem_index;
    op.sem_flg = 0;
    op.sem_op = -1;

    if(-1 == semop(set->id, &op, 1)) {
        perror("sem_wait_index[semop]");
        exit(EXIT_FAILURE);
    }
}

/**
 * Signal a semaphore num_signals times.
 *
 * Params: - Pointer to semaphore set
 *         - Index of semaphore within that set
 *         - Number of times to signal that semaphore. This must be a positive
 *           integer.
 *
 * Side-Effects: If this function fails then the program will be exited.
 */
void sem_signal_index(sem_set_t *set,
                      const int sem_index,
                      const int num_signals) {
    my_sembuf_t op;

    assert(NULL != set);
    assert(num_signals > 0);
    assert(0 <= sem_index && sem_index < set->num_semaphores);

    op.sem_num = sem_index;
    op.sem_flg = 0;
    op.sem_op = num_signals;

    if(-1 == semop(set->id, &op, 1)) {
        perror("sem_init[semop]");
        exit(EXIT_FAILURE);
    }
}
