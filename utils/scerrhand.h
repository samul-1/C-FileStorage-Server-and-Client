#ifndef SC_ERR_HAND_H
#define SC_ERR_HAND_H

#include <stdlib.h>
/**
 * All of the following macros are used to check critical values and exit the program if they match (or don't) a certain value.
 * They're typically used for system calls.
 *
 * Upon failure, the expression that was tested is printed with `perror` and the program is exited with status `EXIT_FAILURE`.
 *
 * The checks themselves are wrapped inside a `do .. while(0)` to allow them to be used safely inside compound statements
 * (e.g. a single-line `if ... else` without brackets)
 */

#define DIE_ON_NZ(v)\
    do {\
        if ((v) && errno != EINTR) {\
            perror(#v);\
            exit(EXIT_FAILURE);\
        }\
    } while (0);

#define DIE_ON_NULL(v)\
    do {\
        if(v == NULL) {\
            perror(#v);\
            exit(EXIT_FAILURE);\
        }\
    } while(0);

#define DIE_ON_NEG_ONE(v)\
    do {\
        if(v == -1) {\
            perror(#v);\
            exit(EXIT_FAILURE);\
        }\
    } while(0);

#endif