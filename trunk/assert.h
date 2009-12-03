/*
 * assert.h
 *
 *  Created on: Dec 2, 2009
 *      Author: petergoodman
 *     Version: $Id$
 */

#ifndef ASSERT_H_
#define ASSERT_H_

#define _QUOTE(x) #x
#define QUOTE(x) _QUOTE(x)

/* defined to use exit instead of abort so that atexit handlers are called. */
#define assert(cond) {if(!(cond)){\
        fprintf(stderr, "Assertion '%s' failed in file %s on line %d.\n", \
            QUOTE(cond), \
            __FILE__, \
            __LINE__ \
        ); \
        exit(EXIT_FAILURE); \
    }}

#endif /* ASSERT_H_ */
