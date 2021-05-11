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

#include "../include/clientApi.h"

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

#define TOO_MANY_P_MSG "You can only enable prints multiple times.\n"
#define TOO_MANY_F_MSG "You can only set the socket name once.\n"
#define ARG_REQUIRED_MSG "Option %c requires an argument.\n"

#define MAX_SOCKETPATH_LEN 1024
#define MAX_ARG_OPT_LEN 1024

char socketName[MAX_SOCKETPATH_LEN] = { 0 };
int socketFd;
bool enablePrints = false;

int main(int argc, char** argv) {
    int c;
    char lastFlag = '\0';
    char real[MAX_SOCKETPATH_LEN];

    while ((c = getopt(argc, argv, ":hf:w:W:D:r:R:d:t:l:u:c:p")) != -1) {
        switch (c) {
        case 'h':
            puts("help");
            return EXIT_SUCCESS;
        case 'f':
            if (strlen(socketName)) {
                fprintf(stderr, TOO_MANY_F_MSG);
                return EXIT_FAILURE;
            }
            if (realpath(optarg, real) == NULL && errno != ENOENT) {
                perror("realpath");
                return EXIT_FAILURE;
            }
            // set socket name
            strncpy(socketName, real, MAX_SOCKETPATH_LEN);
            // ? socketName[strlen(real)] = '\0';
            printf("socket name is %s\n", socketName);
            break;
        case 'w':
            printf("want to write all stuff in %s\n", optarg);
            break;
        case 'W':
            printf("want to write files: %s\n", optarg);
            break;
        case 'D':
            if (tolower(lastFlag) != 'w') {
                puts("D requires w/W");
                return EXIT_FAILURE;
            }
            printf("set return directory for write to %s\n", optarg);
            break;
        case 'r':
            printf("want to read all stuff in %s\n", optarg);
            break;
        case 'R':
            printf("want to read %s files\n", optarg);
            break;
        case 'd':
            if (tolower(lastFlag) != 'r') {
                puts("d requires r/R");
                return EXIT_FAILURE;
            }
            printf("set return directory for read to %s\n", optarg);
            break;
        case 't':
            printf("set interval to %s\n", optarg);
            break;
        case 'l':
            printf("want to lock %s\n", optarg);
            break;
        case 'u':
            printf("want to unlock %s\n", optarg);
            break;
        case 'c':
            printf("want to delete %s\n", optarg);
            break;
        case 'p':
            if (enablePrints) {
                fprintf(stderr, TOO_MANY_P_MSG);
                return EXIT_FAILURE;
            }
            enablePrints = true;
            printf("enabled prints\n");
            break;
        case ':':
            if (optopt == 'R') { // todo handle case like `-R -d` where -d becomes the arg of -R
                puts("R with default arg");
                break;
            }
            else if (optopt == 't') { // todo same as above
                puts("t with 0 delay");
                break;
            }
            else {
                fprintf(stderr, ARG_REQUIRED_MSG, optopt);
                return EXIT_FAILURE;
            }
            break;
        }
        lastFlag = c;
    }
}