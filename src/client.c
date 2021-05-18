#define _DEFAULT_SOURCE

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
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include "../include/clientInternals.h"
#include "../utils/flags.h"

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
#define ARG_NO_NUM "The argument of -%c option must be a number.\n"
#define NO_F_MSG "You must specify a name for the socket with -f.\n"
#define UNKNOWN_ARG_MSG "Unknown option -%c.\n"
#define USAGE_MSG "usage"

#define MAX_SOCKETPATH_LEN 1024
#define MAX_ARG_OPT_LEN 1024

#define RETRY_AFTER_MSEC 1000
#define GIVE_UP_AFTER_SEC 10

#define DEALLOC_AND_FAIL \
deallocParser(cliCommandList);\
return -1;

#define FAIL_IF_NO_ARG(o, c) \
if(!o->argument) { \
    fprintf(stderr, ARG_REQUIRED_MSG, c);\
    DEALLOC_AND_FAIL;\
}

;
// splits the given comma-separated argument and makes an API call for each of the token arguments
#define MULTIARG_API_WRAPPER(apiFunc, arg) \
do {\
    char* strtok_r_savePtr;\
    char* currFile = strtok_r(arg, ",", &strtok_r_savePtr);\
\
    while (currFile) {\
        if (apiFunc(currFile) == -1 && errno != EBADE ) {\
            perror(#apiFunc);\
            return EXIT_FAILURE;\
        }\
            currFile = strtok_r(NULL, ",", &strtok_r_savePtr);\
    }\
} while (0);

// splits the given comma-separated argument and makes an API call for each of the token arguments;
// each API call is wrapped inside a "transaction" that begins with `openFile` and ends with `closeFile`
#define MULTIARG_API_TRANSACTION_WRAPPER(apiFunc, arg, dirname, openFlags) \
do {\
    char* strtok_r_savePtr;\
    char* currFile = strtok_r(arg, ",", &strtok_r_savePtr);\
    while (currFile) {\
        if(openFile(currFile, openFlags) == -1) {\
            if(errno != EBADE) {\
                perror("openFile");\
                return EXIT_FAILURE;\
            }\
        }\
        else if (apiFunc(currFile, dirname) == -1) {\
            if(errno != EBADE) {\
                perror(#apiFunc);\
                return EXIT_FAILURE;\
            }\
        }\
        else if(closeFile(currFile) == -1) {\
            if(errno != EBADE) {\
                perror("closeFile");\
                return EXIT_FAILURE;\
            }\
        }\
        currFile = strtok_r(NULL, ",", &strtok_r_savePtr);\
    }\
} while(0);

int smallrHandler(char* arg, char* dirname) {
    char* strtok_r_savePtr;
    char* currFile = strtok_r(arg, ",", &strtok_r_savePtr);

    while (currFile) {
        char* outBuf = NULL;
        size_t fileSize = 0;
        if (openFile(currFile, O_NOFLAG) == -1) {
            currFile = strtok_r(NULL, ",", &strtok_r_savePtr);
            continue;
        }
        // build `dirname/pathOfFile`
        char* filepathBuf = calloc((dirname ? strlen(dirname) : 0) + strlen(currFile) + 2, 1);
        if (!filepathBuf) {
            return -1;
        }
        if (readFile(currFile, (void**)&outBuf, &fileSize) == -1) {
            // `EBADE` means the request failed on the server-side, so we can just ignore it and continue to the next
            // iteration; any other `errno` value means a system call failed and we need to propagate the error
            if (errno != EBADE) {
                free(filepathBuf);
                return -1;
            }
        }
        else {
            if (dirname) {
                strncpy(filepathBuf, dirname, strlen(dirname));
                filepathBuf[strlen(dirname)] = '/';
                strncpy(filepathBuf + strlen(dirname) + 1, currFile, strlen(currFile));

                if (saveFileToDisk(filepathBuf, outBuf, fileSize) == -1) {
                    perror("saveFileToDisk");
                    free(filepathBuf);
                    return -1;
                }
            }
            else {
                fprintf(stdout, "Read files were thrown away. To store them, use -d.\n");
            }
            if (closeFile(currFile) == -1 && errno != EBADE) {
                perror("closeFile");
                free(filepathBuf);
                return -1;
            }
            free(outBuf);
        }
        currFile = strtok_r(NULL, ",", &strtok_r_savePtr);
        free(filepathBuf);
    }
    return 0;
}

int visitDirAndWrite(char* fromDir, char* dirname, size_t upTo) {
    DIR* targetDir = NULL; // ! warning "might be uninitialized"
    struct dirent* currFile;

    char realFilePath[PATH_MAX];

    char* filePathname; // contains each file's path preceded by the directory name
    size_t fileCount = 0; // files processed so far
    if ((targetDir = opendir(fromDir)) == NULL) {
        return -1;
    }

    errno = 0;
    while ((currFile = readdir(targetDir)) && (!upTo || fileCount < upTo)) {
        struct stat st;
        if (!currFile && errno) {
            return -1;
        }
        // skip current and parent dirs
        if (!strcmp(currFile->d_name, ".") || !strcmp(currFile->d_name, "..")) {
            continue;
        }
        if ((filePathname = calloc(strlen(fromDir) + strlen(currFile->d_name) + 2, 1)) == NULL) {
            return -1;
        }
        strcpy(filePathname, fromDir);
        strcat(filePathname, "/");
        strcat(filePathname, currFile->d_name);

        if (stat(filePathname, &st) == -1) {
            perror("stat");
        }

        if (S_ISDIR(st.st_mode)) {
            visitDirAndWrite(filePathname, dirname, upTo - fileCount);
        }
        else
        {
            realpath(filePathname, realFilePath);
            if (openFile(realFilePath, O_CREATE | O_LOCK) == -1) {
                // `EBADE` means the request failed on the server-side, so we can just ignore it and continue to the next
                // iteration; any other `errno` value means a system call failed and we need to propagate the error
                if (errno != EBADE) {
                    perror("openFile");
                    free(filePathname);
                    return -1;
                }
            }
            else if (writeFile(realFilePath, dirname) == -1) {
                if (errno != EBADE) {
                    perror("writeFile");
                    free(filePathname);
                    return -1;
                }
            }
            else if (closeFile(realFilePath) == -1) {
                if (errno != EBADE) {
                    perror("closeFile");
                    free(filePathname);
                    return -1;
                }
            }
            free(filePathname);
            fileCount += 1;
        }
    }
    if (closedir(targetDir) == -1) {
        perror("closedir");
        return -1;
    }
    return 0;
}

int smallwHandler(char* arg, char* dirname) {
    // todo make the optional argument n=0 instead of parsing just the number
    long upTo = 0;
    char* strtok_r_savePtr;
    char* fromDir = strtok_r(arg, ",", &strtok_r_savePtr);

    char* _upTo = strtok_r(NULL, ",", &strtok_r_savePtr);
    if (isNumber(_upTo, &upTo) != 0) {
        errno = EINVAL;
        return -1;
    }

    return visitDirAndWrite(fromDir, dirname, upTo);
}

int runCommands(CliOption* cliCommandList, long tBetweenReqs, bool validateOnly) {
    while (cliCommandList) {
        bool skipNext = false;

        char* dirname = NULL;
        switch (cliCommandList->option)
        {
        case 'D':
            fprintf(stderr, D_AFTER_W_MSG);
            return -1;
        case 'd':
            fprintf(stderr, d_AFTER_R_MSG);
            return -1;
        case 'w':
            FAIL_IF_NO_ARG(cliCommandList, 'w');
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'D') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'D');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            if (!validateOnly) {
                if (smallwHandler(cliCommandList->argument, dirname) == -1) {
                    return -1;
                }
            }
            break;
        case 'W':
            FAIL_IF_NO_ARG(cliCommandList, 'W');
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'D') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'D');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            if (!validateOnly) {
                MULTIARG_API_TRANSACTION_WRAPPER(writeFile, cliCommandList->argument, dirname, (O_CREATE | O_LOCK));
            }
            break;
        case 'r':
            FAIL_IF_NO_ARG(cliCommandList, 'r');
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'd') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'd');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            if (!validateOnly) {
                if (smallrHandler(cliCommandList->argument, dirname) == -1) {
                    return -1;
                }
            }
            break;
        case 'R':
            ;
            long nArg = 0;

            if (cliCommandList->argument && isNumber(cliCommandList->argument, &nArg) != 0) {
                fprintf(stderr, ARG_NO_NUM, 'R');
                return -1;
            }
            if (cliCommandList->nextPtr && cliCommandList->nextPtr->option == 'd') {
                FAIL_IF_NO_ARG(cliCommandList->nextPtr, 'd');
                dirname = cliCommandList->nextPtr->argument;
                skipNext = true;
            }
            if (!validateOnly) {
                readNFiles(nArg, dirname);
            }
            break;
        case 'l':
            FAIL_IF_NO_ARG(cliCommandList, 'l');
            if (!validateOnly) {
                MULTIARG_API_WRAPPER(lockFile, cliCommandList->argument);
            }
            break;
        case 'u':
            FAIL_IF_NO_ARG(cliCommandList, 'u');
            if (!validateOnly) {
                MULTIARG_API_WRAPPER(unlockFile, cliCommandList->argument);
            }
            break;
        case 'c':
            FAIL_IF_NO_ARG(cliCommandList, 'c');
            if (!validateOnly) {
                MULTIARG_API_WRAPPER(removeFile, cliCommandList->argument);
            }
            break;
        default:
            fprintf(stderr, UNKNOWN_ARG_MSG, cliCommandList->option);
            return -1;
            break;
        }
        cliCommandList = cliCommandList->nextPtr;
        if (skipNext && cliCommandList) {
            cliCommandList = cliCommandList->nextPtr;
        }

        usleep(1000 * tBetweenReqs);
    }
    return 0;
}

int main(int argc, char** argv) {
    CliOption* cliCommandList = parseCli(argc, argv);
    long tBetweenReqs = 0;

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
    char sockname[MAX_SKT_PATH] = "";

    if ((currOpt = popOption(&cliCommandList, 'h'))) {
        fprintf(stdout, USAGE_MSG);
        deallocOption(currOpt);
        deallocParser(cliCommandList);
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
        strncpy(sockname, currOpt->argument, MAX_SOCKETPATH_LEN);
        deallocOption(currOpt);
        if ((currOpt = popOption(&cliCommandList, 'f'))) { // check if there is a second -f command
            fprintf(stderr, TOO_MANY_F_MSG);
            deallocOption(currOpt);
            DEALLOC_AND_FAIL;
        }
    }

    if ((currOpt = popOption(&cliCommandList, 'p'))) {
        PRINTS_ENABLED = true;
        deallocOption(currOpt);
        if ((currOpt = popOption(&cliCommandList, 'p'))) { // check if there is a second -p command
            fprintf(stderr, TOO_MANY_P_MSG);
            deallocOption(currOpt);
            DEALLOC_AND_FAIL;
        }
    }

    if ((currOpt = popOption(&cliCommandList, 't'))) {
        if (currOpt->argument && (isNumber(currOpt->argument, &tBetweenReqs) != 0)) {
            fprintf(stderr, ARG_NO_NUM, 't');
            deallocOption(currOpt);
            DEALLOC_AND_FAIL;
        }
        deallocOption(currOpt);
        if ((currOpt = popOption(&cliCommandList, 't'))) { // check if there is a second -t command
            fprintf(stderr, TOO_MANY_T_MSG);
            deallocOption(currOpt);
            DEALLOC_AND_FAIL;
        }
    }

    // validate the commands before running any of them
    if (runCommands(cliCommandList, 0, true) == -1) {
        DEALLOC_AND_FAIL;
    }

    time_t now = time(0);
    int msec = RETRY_AFTER_MSEC;
    struct timespec abstime = { .tv_nsec = 0, .tv_sec = now + GIVE_UP_AFTER_SEC };

    if (openConnection(sockname, msec, abstime) == -1) {
        perror("open connection");
        deallocParser(cliCommandList);
        return EXIT_FAILURE;
    }
    // now actually run the commands
    runCommands(cliCommandList, tBetweenReqs, false); // we don't actually check errors; upon return we dealloc and close connection regardless
    deallocParser(cliCommandList);

    if (closeConnection(sockname) == -1) {
        perror("close connection");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}