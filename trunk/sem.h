/*
 * sem.h
 *
 *  Created on: Dec 1, 2009
 *      Author: petergoodman
 *     Version: $Id$
 */

#ifndef SEM_H_
#define SEM_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <alloca.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "assert.h"

/* Represents a set of UNIX semaphores. */
typedef struct {
    int id;
    int num_semaphores;
} sem_set_t;

/* Relates a single semaphore to the semaphore set that it belongs to */
typedef struct {
    sem_set_t *set;
    int num;
} sem_t;

/* operations on sets of semaphores */
void sem_fill_set(sem_set_t *set, const int num_semaphores);
void sem_empty_set(sem_set_t *set);
void sem_unpack_set(sem_set_t *set, sem_t *sem1, ...);

void sem_init_all(sem_set_t *set, const int value);
void sem_signal_all(sem_set_t *set, const int num_signals);

/* operations on individual semaphores */
void sem_init_index(sem_set_t *set, const int sem_index, const int value);
void sem_wait_index(sem_set_t *set, const int which_semaphore);
void sem_signal_index(sem_set_t *set,
                      const int sem_index,
                      const int num_signals);

#define sem_init(sem, val) sem_init_index((sem).set, (sem).num, (val))
#define sem_wait(sem) sem_wait_index((sem).set, (sem).num)
#define sem_signal(sem) sem_signal_index((sem).set, (sem).num, 1)
#define sem_signal_ntimes(sem, n) sem_signal_index((sem).set, (sem).num, (n))

#define CRITICAL(sem, context) {sem_wait(sem);{context}sem_signal(sem);}

#endif /* SEM_H_ */
