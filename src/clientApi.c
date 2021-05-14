#include "../include/clientApi.h"
#include "../include/requestCode.h"
#include "../include/responseCode.h"
#include "../utils/scerrhand.h"
#include "../include/clientServerProtocol.h"
#include "../utils/misc.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <limits.h>


bool PRINTS_ENABLED = false;

char* realpath(const char* restrict path,
    char* restrict resolved_path);

const char* errMessages[] = {
    "OK",
    "File not found.",
    "Operation not permitted.",
    "File is too big to be stored.",
    "An error on the server-side occurred.",
    "Invalid request code or payload.",
    "File already exists.",
};

const int errnoMap[] = {
    0, ENOENT, EACCES, E2BIG, EINVAL, EFAULT
};

#define INITIAL_REQ_SIZ 1024

#define PRINT_IF_ENABLED(fd, op, filepath, msg) \
if(PRINTS_ENABLED) {\
    fprintf(fd, "%s '%s': %s\n", #op, filepath, msg);\
}

#define PRINT_ERR_IF_ENABLED(op, filepath, errCode) \
PRINT_IF_ENABLED(stderr, op, filepath, errMessages[errCode-1]);

#define ERR_CODE_TO_ERRNO(errCode) (errno = errnoMap[errCode-1]);

/* static char* constructRequest(int reqCode, char* segment1, char* segment2) {
    size_t reqLen = INITIAL_REQ_SIZ;
    size_t segmentLen;
    size_t oldLen = reqLen;

    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return NULL;
    }
    segmentLen = strlen(segment1);

    reqLen = MAX(METADATA_SIZE + REQ_CODE_LEN + segmentLen + 1, reqLen);
    if (reqLen > oldLen) {
        void* tmp = realloc(req, METADATA_SIZE + REQ_CODE_LEN + segmentLen + 1);
        if (!tmp) {
            free(req);
            errno = ENOMEM;
            return NULL;
        }
        req = tmp;
    }
    snprintf(req, reqLen+1, "%d%08ld%s", reqCode, segmentLen, segment1);

    if (segment2) {
        segmentLen = strlen(segment2);
    }
}*/


static int _mkdir(const char* dir) {
    /**
     * @brief Recursively creates directories
     *
     * @note Adapted from http://nion.modprobe.de/blog/archives/357-Recursive-directory-creation.html
     *
     */
    char tmp[256];
    char* p = NULL;
    size_t len;
    puts(dir);
    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, S_IRWXU) == -1) {
                return -1;
            };
            *p = '/';
        }
    mkdir(tmp, S_IRWXU);
    return 0;
}

static int storeFiles(const char* dirname) {
    /**
     * todo write docs
     */
    char filemetadataBuf[METADATA_SIZE + 1] = "";
    int count = 0;
    while (true) {
        // read length of filepath
        DIE_ON_NEG_ONE(read(SOCKET_FD, filemetadataBuf, METADATA_SIZE));
        size_t filepathLen = atol(filemetadataBuf);

        if (filepathLen == 0) {
            // no more files to read
            break;
        }

        count += 1;

        char* filepathBuf = calloc(filepathLen + strlen(dirname) + 2, 1);
        if (!filepathBuf) {
            errno = ENOMEM;
            return -1;
        }

        // read path of the file
        DIE_ON_NEG_ONE(read(SOCKET_FD, filepathBuf + strlen(dirname) + 1, filepathLen));
        // prepend the argument `dirname` + /
        strncpy(filepathBuf, dirname, strlen(dirname));
        filepathBuf[strlen(dirname)] = '/';

        // split file name from rest of path and recursively create the directories
        char* lastSlash = strrchr(filepathBuf, '/');
        if (lastSlash) {
            *lastSlash = '\0';
            // just pass the directories without the filename
            if (_mkdir(filepathBuf) == -1) {
                int errnosave = errno;
                free(filepathBuf);
                errno = errnosave;
                return -1;
            }
            *lastSlash = '/';
        }

        // read size of content of the file
        DIE_ON_NEG_ONE(read(SOCKET_FD, filemetadataBuf, METADATA_SIZE));
        size_t filecontentLen = atol(filemetadataBuf);
        char* filecontentBuf = calloc(filecontentLen + 1, 1);
        if (!filecontentBuf) {
            free(filepathBuf);
            errno = ENOMEM;
            return -1;
        }
        // read content of the file
        // todo use readn
        DIE_ON_NEG_ONE(read(SOCKET_FD, filecontentBuf, filecontentLen));

        FILE* fp = fopen(filepathBuf, "w+");
        if (fp == NULL) {
            return -1;
        }
        if (fwrite(filecontentBuf, 1, filecontentLen, fp) <= 0) {
            int errnosave = errno;
            if (ferror(fp)) {
                errno = errnosave;
                return -1;
            }
        }
        fclose(fp);

        free(filecontentBuf);
        free(filepathBuf);
    }
    return count;
}

//int openConnection(const char* sockname, int msec, const struct timespec abstime);

//int closeConnection(const char* sockname);

int openFile(const char* pathname, int flags) {
    size_t pathnameLen = strlen(pathname);
    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + OPEN_FLAG_LEN + pathnameLen + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }
    // construct request message
    snprintf(req, reqLen, "%d%08ld%s%d", OPEN_FILE, pathnameLen, pathname, flags);
    // todo use writen
    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }
    puts(req);


    free(req);

    char recvLine[RES_CODE_LEN + 1] = "";
    // wait for response
    if (read(SOCKET_FD, recvLine, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;

    if (isNumber(recvLine, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Open, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    if (responseCode == OK) {
        PRINT_IF_ENABLED(stdout, Open, pathname, "OK\n");
    }
    else {
        PRINT_ERR_IF_ENABLED(Open, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }

    return 0;
}

int readNFiles(int N, const char* dirname) {
    char req[METADATA_SIZE + REQ_CODE_LEN + 1];
    snprintf(req, METADATA_SIZE + REQ_CODE_LEN + 1, "%d%08d", READ_N_FILES, N);

    if (write(SOCKET_FD, req, METADATA_SIZE + REQ_CODE_LEN) == -1) {
        return -1;
    }
    puts("WRITTEN TO SOCKET");
    puts(req);
    char recvLine1[RES_CODE_LEN + 1] = "";

    // wait for response
    if (read(SOCKET_FD, recvLine1, RES_CODE_LEN) == -1) {
        return -1;
    }
    printf("I JUST READ %s\n", recvLine1);
    long responseCode;
    if (isNumber(recvLine1, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, ReadNFiles, "", "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    else if (responseCode != OK) { // there's nothing else to read in the response
        PRINT_ERR_IF_ENABLED(ReadNFiles, "", responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }

    storeFiles(dirname);
    return 0;
}

int readFile(const char* pathname, void** buf, size_t* size) {
    size_t pathnameLen = strlen(pathname);
    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + pathnameLen + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }

    // construct request message
    snprintf(req, reqLen + 1, "%d%08ld%s", READ_FILE, pathnameLen, pathname);

    // todo use writen
    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }

    free(req);

    char
        recvLine1[RES_CODE_LEN + 1], // for response code
        recvLine2[METADATA_SIZE + 1], // to get the size of the payload
        * recvLine3; // for the rest of the response

    // wait for response
    if (read(SOCKET_FD, recvLine1, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;
    if (isNumber(recvLine1, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Read, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    else if (responseCode != OK) { // there's nothing else to read in the response
        PRINT_ERR_IF_ENABLED(Read, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }

    // read size of the response content
    if (read(SOCKET_FD, recvLine2, METADATA_SIZE) == -1) {
        return -1;
    }
    long responseSize;
    if (isNumber(recvLine2, &responseSize) != 0) {
        PRINT_IF_ENABLED(stderr, Read, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }

    recvLine3 = calloc(responseSize + 1, 1);
    if (!recvLine3) {
        return -1;
    }
    // todo use readn
    if (read(SOCKET_FD, recvLine3, responseSize) == -1) { // read the actual content of the file
        return -1;
    }

    *size = responseSize;
    *buf = recvLine3;

    return 0;
}

int writeFile(const char* pathname, const char* dirname) {
    size_t pathnameLen = strlen(pathname);
    FILE* fp = fopen(pathname, "r");
    if (!fp) {
        return -1;
    }

    if (fseek(fp, 0L, SEEK_END) == -1) {
        return -1;
    }
    size_t filecontentLen = ftell(fp);
    rewind(fp);

    char* filecontentBuf = calloc(filecontentLen + 1, 1);
    if (!filecontentBuf) {
        return -1;
    }
    if (fread(filecontentBuf, sizeof(char), filecontentLen, fp) <= 0) {
        int errnosave = errno;
        if (ferror(fp)) {
            errno = errnosave;
            return -1;
        }
    }

    fclose(fp);

    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + pathnameLen + METADATA_SIZE + filecontentLen + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }

    // todo change this to handle binary data
    // construct request message
    snprintf(req, reqLen + 1, "%d%08ld%s%08ld%s", WRITE_FILE, pathnameLen, pathname, filecontentLen, filecontentBuf);
    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }

    free(req);

    char recvLine[RES_CODE_LEN + 1];
    // wait for response
    if (read(SOCKET_FD, recvLine, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;

    if (isNumber(recvLine, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Write, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    if (responseCode == OK) {
        PRINT_IF_ENABLED(stdout, Write, pathname, "OK");
    }
    else {
        PRINT_ERR_IF_ENABLED(Write, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }

    if (storeFiles(dirname) == -1) {
        return -1;
    }
    return 0;
}


int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname) {
    if (pathname == NULL || buf == NULL || size == 0 || dirname == NULL) {
        errno = EINVAL;
        return -1;
    }

    size_t pathnameLen = strlen(pathname);
    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + pathnameLen + METADATA_SIZE + size + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        return -1;
    }
    snprintf(req, reqLen + 1, "%d%08ld%s%08ld%s", APPEND_TO_FILE, pathnameLen, pathname, size, (char*)buf);

    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }

    free(req);

    char recvLine[RES_CODE_LEN + 1] = "";
    // wait for response
    if (read(SOCKET_FD, recvLine, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;

    if (isNumber(recvLine, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Append, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    if (responseCode == OK) {
        PRINT_IF_ENABLED(stdout, Append, pathname, "OK");
    }
    else {
        PRINT_ERR_IF_ENABLED(Append, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }

    if (storeFiles(dirname) == -1) {
        return -1;
    }
    return 0;
}

int lockFile(const char* pathname) {
    size_t pathnameLen = strlen(pathname);
    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + pathnameLen + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }

    // construct request message
    snprintf(req, reqLen + 1, "%d%08ld%s", LOCK_FILE, pathnameLen, pathname);
    // puts(req);

    // todo use writen
    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }

    free(req);

    char recvLine[RES_CODE_LEN + 1] = "";
    // wait for response
    if (read(SOCKET_FD, recvLine, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;

    if (isNumber(recvLine, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Lock, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    if (responseCode == OK) {
        PRINT_IF_ENABLED(stdout, Lock, pathname, "OK");
    }
    else {
        PRINT_ERR_IF_ENABLED(Lock, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }
    return 0;
}

int unlockFile(const char* pathname) {
    size_t pathnameLen = strlen(pathname);
    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + pathnameLen + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }

    // construct request message
    snprintf(req, reqLen + 1, "%d%08ld%s", UNLOCK_FILE, pathnameLen, pathname);
    // puts(req);

    // todo use writen
    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }

    free(req);

    char recvLine[RES_CODE_LEN + 1];
    // wait for response
    if (read(SOCKET_FD, recvLine, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;

    if (isNumber(recvLine, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Unlock, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    if (responseCode == OK) {
        PRINT_IF_ENABLED(stdout, Unlock, pathname, "OK");
    }
    else {
        PRINT_ERR_IF_ENABLED(Unlock, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }
    return 0;
}

int closeFile(const char* pathname) {
    size_t pathnameLen = strlen(pathname);
    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + pathnameLen + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }

    // construct request message
    snprintf(req, reqLen + 1, "%d%08ld%s", CLOSE_FILE, pathnameLen, pathname);
    // puts(req);

    // todo use writen
    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }

    free(req);

    char recvLine[RES_CODE_LEN + 1];
    // wait for response
    if (read(SOCKET_FD, recvLine, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;

    if (isNumber(recvLine, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Close, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    if (responseCode == OK) {
        PRINT_IF_ENABLED(stdout, Close, pathname, "OK");
    }
    else {
        PRINT_ERR_IF_ENABLED(Close, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }
    return 0;
}

int removeFile(const char* pathname) {
    size_t pathnameLen = strlen(pathname);
    size_t reqLen = REQ_CODE_LEN + METADATA_SIZE + pathnameLen + 1;
    char* req = calloc(reqLen, 1);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }

    // construct request message
    snprintf(req, reqLen + 1, "%d%08ld%s", REMOVE_FILE, pathnameLen, pathname);
    // puts(req);

    // todo use writen
    if (write(SOCKET_FD, req, reqLen - 1) == -1) {
        return -1;
    }

    free(req);

    char recvLine[RES_CODE_LEN + 1];
    // wait for response
    if (read(SOCKET_FD, recvLine, RES_CODE_LEN) == -1) {
        return -1;
    }
    long responseCode;

    if (isNumber(recvLine, &responseCode) != 0) {
        PRINT_IF_ENABLED(stderr, Remove, pathname, "Invalid response from server.\n");
        errno = EINVAL;
        return -1;
    }
    if (responseCode == OK) {
        PRINT_IF_ENABLED(stdout, Remove, pathname, "OK");
    }
    else {
        PRINT_ERR_IF_ENABLED(Remove, pathname, responseCode);
        ERR_CODE_TO_ERRNO(responseCode);
        return -1;
    }
    return 0;
}
