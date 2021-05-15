#include "cliParser.h"
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define EXPECTING_OPT 1
#define EXPECTING_ARG 2

struct cliOption {
    char option;
    char* argument;
    struct cliOption* nextPtr;
};


void printParser(CliOption* optList) {
    CliOption* currPtr = optList;
    while (currPtr) {
        printf("Option %c, argument: %s\n", currPtr->option, currPtr->argument);
        currPtr = currPtr->nextPtr;
    }
}

static void pushOpt(CliOption** optList, CliOption* option) {
    CliOption** target = optList;
    while (*target) {
        target = &((*target)->nextPtr);
    }
    *target = option;
}

CliOption* popOption(CliOption* optList, char optName) {
    CliOption* prevPtr = NULL, * currPtr = optList;
    while (currPtr) {
        if (currPtr->option == optName) {
            if (prevPtr) {
                prevPtr->nextPtr = currPtr->nextPtr;
            }
            return currPtr;
        }
        prevPtr = currPtr;
        currPtr = currPtr->nextPtr;
    }
    return NULL;
}

static CliOption* getLastOpt(CliOption* optList) {
    if (!optList) {
        return NULL;
    }

    CliOption* currPtr = optList;
    while (currPtr->nextPtr) {
        currPtr = currPtr->nextPtr;
    }
    return currPtr;
}

static int addArgumentToLastOpt(CliOption* optList, char* argument) {
    CliOption* lastOptPtr = getLastOpt(optList);
    assert(lastOptPtr);

    lastOptPtr->argument = calloc(strlen(argument) + 1, 1);
    if (!lastOptPtr->argument) {
        return -1;
    }
    strcpy(lastOptPtr->argument, argument);
    return 0;
}

int deallocParser(CliOption* optList) {
    if (!optList) {
        errno = EINVAL;
        return -1;
    }
    CliOption* tmpPtr;
    while (optList) {
        tmpPtr = optList;
        optList = optList->nextPtr;
        if (tmpPtr->argument) {
            free(tmpPtr->argument);
        }
        free(tmpPtr);
    }
    return 0;
}

static CliOption* allocOption(char optname) {
    CliOption* newOpt = calloc(sizeof(*newOpt), 1);
    if (!newOpt) {
        return NULL;
    }
    // first non-hyphen character we've read after a hyphen is the option name
    newOpt->option = optname;
    newOpt->argument = NULL;
    newOpt->nextPtr = NULL;
    return newOpt;
}

CliOption* parseCli(int numStrings, char** strings) {
    /**
     * @brief Parses an array of `numStrings` strings representing command line \n
     * options and their arguments. Supports single-character arguments only (the \n
     * characters after the first one are ignored) and optional arguments.
     *
     * @note Options' arguments cannot contain spaces unless they are wrapped in quotes
     *
     * @return A list containing the parsed options and arguments. The list \n
     * is allocated on the heap and needs to be `free`d later. Returns NULL if memory \n
     * for one of the options couldn't be allocated.
     *
     */
    CliOption* optList = NULL;

    short currState = EXPECTING_OPT;

    for (size_t i = 0; i < numStrings; i++) {
        size_t currStrIdx = 0;
        if (strings[i][currStrIdx++] == '-') {
            while (strings[i][currStrIdx] == '-') { // read hyphens
                currStrIdx++;
            }
            // first non-hyphen character after the last hyphen
            CliOption* newOpt = allocOption(strings[i][currStrIdx]);
            if (!newOpt) {
                int errnosave = errno;
                deallocParser(optList);
                errno = errnosave;
                return NULL;
            }
            currState = EXPECTING_ARG;
            pushOpt(&optList, newOpt);
        }
        else {
            if (currState == EXPECTING_OPT) {
                // we were expecting an option but we got a dangling string that didn't start with
                // a hyphen: we're just gonna ignore it
                continue;
            }
            // this is the argument of the last option we parsed
            addArgumentToLastOpt(optList, strings[i]);
            currState = EXPECTING_OPT;
        }
    }
    return optList;
}