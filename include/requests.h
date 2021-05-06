/**
 * Defines the data structure representing a client request, as well
 * as a function to serialize the request for writing it to socket
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#define COMMANDLEN 10
#define MAXPATHNAME 1024
#define MAXFLAGLEN 10
#define MAXREQLEN 10+1024+2+10


typedef struct clientRequest {
    char command[COMMANDLEN];
    char pathname[MAXPATHNAME];
    int flags;
} ClientRequest_t;

int serializeRequest(ClientRequest_t req, char* dest) {
    /**
     * @brief Converts a ClientRequest_t to a string according\n
     * to the format supported by the server
     *
     * @param req A pointer to the request to serialize
     * @param dest A buffer to store the serialized request
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * Upon error, `errno` will contain:
     * `EINVAL` for invalid input parameter(s)
     */

    if (!dest) {
        errno = EINVAL;
        return -1;
    }

    char buf[MAXREQLEN];
    char flags[MAXFLAGLEN];
    strncpy(buf, req.command, COMMANDLEN);
    strncat(buf, ";", 1);
    strncat(buf, req.pathname, strlen(req.pathname));

    if (req.flags >= 0) { // -1 means the request has no flags
        strncat(buf, ";", 1);
        sprintf(flags, "%d", req.flags); // convert numerical value to string
        strncat(buf, flags, strlen(flags));
    }

    strncpy(dest, buf, MAXREQLEN);
    return 0;
}