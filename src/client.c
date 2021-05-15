#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>

#include "../utils/misc.h"
#include "../include/clientApi.h"
#include "../include/cliParser.h"

char* realpath(const char* restrict path,
    char* restrict resolved_path);

// #include "../include/clientApi.h"

/**
 * Supported flags:
 *
 * -h (help)
 * -f filename (set socket name)
 * -w dirname [,n=0] (send files in `dirname`, up to `n`, or all files in the directory)
 * -W file1 [,file2] (send file1, ..., fileN)
 * -D dirname (set `dirname` as target for files sent from server in response to -w/-W)
 * -r file1 [,file2] (send read request for file1, ..., fileN)
 * -R [n=0] (send read request for `n` files, or all files on the server)
 * -d dirname (set `dirname` as target for files sent from server in response to -r/-R)
 * -t time (set time interval in between requests)
 * -l file1 [,file2] (send lock request for file1, ..., fileN)
 * -u file1 [,file2] (send unlock request for file1, ..., fileN)
 * -c file1 [,file2] (send delete request for file1, ..., fileN)
 * -p (enable prints for info and errors)
 *
 */

#define TOO_MANY_P_MSG "You can only enable prints once.\n"
#define TOO_MANY_T_MSG "You can only set -t once.\n"
#define TOO_MANY_F_MSG "You can only set the socket name once.\n"
#define d_AFTER_R_MSG "You can only use the -d option after -r or -R\n"
#define D_AFTER_W_MSG "You can only use the -D option after -w or -W\n"
#define ARG_REQUIRED_MSG "Option %c requires an argument.\n"
#define NO_CMD_MSG "No commands were given.\n"
#define T_ARG_NO_NUM "The argument of -t option must be a number.\n"
#define NO_F_MSG "You must specify a name for the socket with -f.\n"
#define UNKNOWN_ARG_MSG "Unknown option -%c.\n"
#define USAGE_MSG "usage"

#define MAX_SOCKETPATH_LEN 1024
#define MAX_ARG_OPT_LEN 1024

#define DEALLOC_AND_FAIL \
deallocOption(cliCommandList);\
return EXIT_FAILURE;

#define FAIL_IF_NO_ARG(o, c) \
if(!o->argument) { \
    fprintf(stderr, ARG_REQUIRED_MSG, c);\
    DEALLOC_AND_FAIL;\
}

char SOCKET_PATH[MAX_SOCKETPATH_LEN];

int smallwHandler(char* arg, char* dirname) {}
int capitalWHandler(char* arg, char* dirname) {}
int smallrHandler(char* arg, char* dirname) {}
int capitalRHandler(char* arg, char* dirname) {}
int LHandler(char* arg) {}
int UHandler(char* arg) {}
int CHandler(char* arg) {}

int main(int argc, char** argv) {
    CliOption* cliCommandList = parseCli(argc, argv);
    long timeInBetweenReqs = 0;

    if (!cliCommandList) {
        if (errno) {
            perror("Parsing CLI commands");
        }
        else {
            fprintf(stderr, NO_CMD_MSG);
        }
        return EXIT_FAILURE;
    }

    CliOption* currOpt;

    if (popOption(&cliCommandList, 'h')) {
        fprintf(stdout, USAGE_MSG);
        return EXIT_SUCCESS;
    }

    if (!(currOpt = popOption(&cliCommandList, 'f'))) {
        fprintf(stderr, NO_F_MSG);
        DEALLOC_AND_FAIL;
    }
    else {
        if (!currOpt->argument) {
            fprintf(stderr, ARG_REQUIRED_MSG, 'f');
            DEALLOC_AND_FAIL;
        }
        strncpy(SOCKET_PATH, currOpt->argument, MAX_SOCKETPATH_LEN);
        if (popOption(&cliCommandList, 'f')) { // check if there is a second -f command
            fprintf(stderr, TOO_MANY_F_MSG);
            DEALLOC_AND_FAIL;
        }
    }

    if (popOption(&cliCommandList, 'p')) {
        PRINTS_ENABLED = true;
        if (popOption(&cliCommandList, 'p')) { // check if there is a second -p command
            fprintf(stderr, TOO_MANY_P_MSG);
            DEALLOC_AND_FAIL;
        }
    }

    if ((currOpt = popOption(&cliCommandList, 't'))) {
        if (currOpt->argument && (isNumber(currOpt->argument, &timeInBetweenReqs) != 0)) {
            fprintf(stderr, T_ARG_NO_NUM);
            DEALLOC_AND_FAIL;
        }
        if (popOption(&cliCommandList, 't')) { // check if there is a second -t command
            fprintf(stderr, TOO_MANY_T_MSG);
            DEALLOC_AND_FAIL;
        }
    }


    while (cliCommandList) {
        bool skipNext = false;

        char* dirname = NULL;
        switch (cliCommandList->option)
        {
        case 'D':
            fprintf(stderr, D_AFTER_W_MSG);
            DEALLOC_AND_FAIL;
        case 'd':
            fprintf(stderr, d_AFTER_R_MSG);
            DEALLOC_AND_FAIL;
        case 'w':
            FAIL_IF_NO_ARG(cliCommandList, 'w');
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'D') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'D');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            smallwHandler(cliCommandList->argument, dirname);
            break;
        case 'W':
            FAIL_IF_NO_ARG(cliCommandList, 'W');
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'D') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'D');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            capitalWHandler(cliCommandList->argument, dirname);
            break;
        case 'r':
            FAIL_IF_NO_ARG(cliCommandList, 'r');
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'd') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'd');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            smallrHandler(cliCommandList->argument, dirname);
            break;
        case 'R':
            FAIL_IF_NO_ARG(cliCommandList, 'R');
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'd') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'd');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            capitalRHandler(cliCommandList->argument, dirname);
            break;
        case 'l':
            FAIL_IF_NO_ARG(cliCommandList, 'l');
            LHandler(cliCommandList->argument);
            break;
        case 'u':
            FAIL_IF_NO_ARG(cliCommandList, 'u');
            UHandler(cliCommandList->argument);
            break;
        case 'c':
            FAIL_IF_NO_ARG(cliCommandList, 'c');
            CHandler(cliCommandList->argument);
            break;
        default:
            fprintf(stderr, UNKNOWN_ARG_MSG, cliCommandList->option);
            return EXIT_FAILURE;
            break;
        }
        cliCommandList = cliCommandList->nextPtr;
        if (skipNext && cliCommandList) {
            cliCommandList = cliCommandList->nextPtr;
        }
    }

}