#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <signal.h>
#include "../include/cacheFns.h"
#include "../include/boundedbuffer.h"
#include "../include/fileparser.h"
#include "../utils/scerrhand.h"
#include "../include/filesystemApi.h"
#include "../include/requestCode.h"
#include "../include/responseCode.h"
#include "../utils/flags.h"
#include "../include/log.h"
#include "../utils/misc.h"
#include "../include/clientServerProtocol.h"
#include <errno.h>

#define UNIX_PATH_MAX 108
#define MAX_CONN 10
#define MAX_TASKS 2048

#define PIPE_BUF_LEN 5

#define DFL_POOLSIZE 10
#define DFL_MAXSTORAGECAP 10000
#define DFL_MAXFILECOUNT 100
#define DFL_SOCKNAME "serversocket.sk"
#define DFL_LOGFILENAME "logs.txt"
#define DFL_SOCKETBACKLOG 10
#define DFL_TASKBUFSIZE 2048
#define DFL_LOGBUFSIZE 2048
#define DFL_REPLACEMENTALGO 0

#define STAT_MSG \
ANSI_COLOR_BG_GREEN "       " ANSI_COLOR_RESET " Statistics: " ANSI_COLOR_BG_GREEN "       " ANSI_COLOR_RESET "\n"\
ANSI_COLOR_CYAN "Max number of files reached: " ANSI_COLOR_RESET "%zu\n" \
ANSI_COLOR_CYAN "Max total storage size reached: " ANSI_COLOR_RESET "%zu\n" \
ANSI_COLOR_CYAN "Number of files that have been evicted from the cache: " ANSI_COLOR_RESET "%zu\n" \
ANSI_COLOR_CYAN "Number of files in the storage at the time of exit: " ANSI_COLOR_RESET "%zu\n"

#define SEND_EVICTED_FILE(fd, file) \
snprintf(evictedBuf, METADATA_SIZE+strlen(file->pathname)+METADATA_SIZE+(file->contentSize)+1, "%08ld%s%08ld%s", strlen(file->pathname), file->pathname, file->contentSize, file->content);\
puts(evictedBuf);\
printf("writing %zu bytes\n", METADATA_SIZE + strlen(file->pathname) + METADATA_SIZE + file->contentSize);\
DIE_ON_NEG_ONE(writen(fd, evictedBuf, (METADATA_SIZE + strlen(file->pathname) + METADATA_SIZE + file->contentSize)));

#define SEND_RESPONSE_CODE(fd, code) \
printf("SENDING CODE %d\n", code);\
snprintf(codeBuf, RES_CODE_LEN + 1, "%d", code);\
DIE_ON_NEG_ONE(write(fd, codeBuf, RES_CODE_LEN));

#define HANDLE_REQ_ERROR(fd) \
switch(errno) {\
case ENOENT:\
    SEND_RESPONSE_CODE(fd, FILE_NOT_FOUND);\
    break;\
case EACCES:\
    SEND_RESPONSE_CODE(fd, FORBIDDEN);\
    break;\
case EPERM:\
    SEND_RESPONSE_CODE(fd, ALREADY_EXISTS);\
    break;\
case E2BIG:\
    SEND_RESPONSE_CODE(fd, FILE_TOO_BIG);\
    break;\
case EINVAL:\
    SEND_RESPONSE_CODE(fd, BAD_REQUEST);\
    break;\
}

#define GET_LONGVAL_OR_EXIT(p, k, v, d, failcond)\
do {\
    errno = 0;\
    if(((v = getLongValueFor(p, k, d)) == -1) && errno != 0) {\
        fprintf(stderr, "Error getting value for " #k "; using default.\n");\
        v = d;\
    }\
    if(v failcond) {\
        fprintf(stderr, "Invalid value for config parameter " #k "; using default.\n");\
        v = d;\
    }\
} while(0);

#define GET_VAL_OR_EXIT(p, k, b, d)\
    if(getValueFor(p, k, b, d) == -1) {\
    perror("Error getting value for " #k);\
    if(p) {\
        destroyParser(p);\
    }\
    exit(EXIT_FAILURE);\
}


#define NOTIFY_PENDING_CLIENTS(notifyList, notifyCode, pipeBuf, pipeOut)\
while (notifyList) {\
    SEND_RESPONSE_CODE(notifyList->fd, notifyCode);\
    snprintf(pipeBuf, PIPE_BUF_LEN, "%04d", notifyList->fd);\
    printf("notifying %s\n", pipeBuf);\
    DIE_ON_NEG_ONE(write(pipeOut, pipeBuf, PIPE_BUF_LEN));\
    struct fdNode* tmpPtr = notifyList;\
    notifyList = notifyList->nextPtr;\
    free(tmpPtr);\
}

#define NO_MORE_CONTENT "00000000"
#define CLIENT_LEFT_MSG "0000"

char* getRequestPayloadSegment(int fd) {
    /**
     * @brief Takes in an fd that has written a segment of a request payload, reads the payload, and returns it.
     * @note This function assumes the fd has written a string that abides by the protocol: \n
     * i.e. the length of the segment (normalized to 8 digits) immediately followed by a string of that length
     * @note This function allocates memory on the heap that the caller must later call `free` on.
     *
     *
     * @param fd The fd to read.
     *
     * @return A string containing the read segment
     *
     */
    char
        metadataBuf[METADATA_SIZE + 1] = "",
        * recvBuf;

    DIE_ON_NEG_ONE(readn(fd, metadataBuf, METADATA_SIZE)); // todo better error handling
    size_t filenameLen = atol(metadataBuf);

    // now we have enough memory to store the filename
    DIE_ON_NULL((recvBuf = calloc(filenameLen + 1, 1)));
    DIE_ON_NEG_ONE(readn(fd, recvBuf, filenameLen));

    return recvBuf;
}

struct workerArgs {
    BoundedBuffer* buf;
    CacheStorage_t* store;
    int pipeOut;
};

volatile sig_atomic_t softExit = 0;
volatile sig_atomic_t hardExit = 0;

void unlinkSock() {
    // !!! remove this
    unlink(DFL_SOCKNAME);
}

void exitSigHandler(int sig) {
    // ! if sig == SIGINT || sig == SIGTSTP then hardexit = 1, if sig == SIGINT then softexit = 1
    if (sig == SIGINT || sig == SIGHUP) {
        softExit = 1;
    }
    else
        hardExit = 1;
}

ssize_t updateClientCount(ssize_t delta) {
    static pthread_mutex_t countMutex = PTHREAD_MUTEX_INITIALIZER;
    static ssize_t count = 0;
    size_t ret;

    DIE_ON_NEG_ONE(pthread_mutex_lock(&countMutex));
    count += delta;
    ret = count;
    DIE_ON_NEG_ONE(pthread_mutex_unlock(&countMutex));
    return ret;
}

#define GET_CLIENT_COUNT updateClientCount(0)


void* _startWorker(void* args) {
    /*
    Upon being called, enters an infinite loop and
    reads tasks from the queue, processing them one at a time
    */
    BoundedBuffer* taskBuf = ((struct workerArgs*)args)->buf;
    CacheStorage_t* store = ((struct workerArgs*)args)->store;
    int pipeOut = ((struct workerArgs*)args)->pipeOut;

    while (true) {
        int
            rdy_fd = 0,
            newLock = 0;

        char codeBuf[RES_CODE_LEN + 1] = "";

        ssize_t numRead = 0;
        bool putFdBack = true;

        char
            requestCodeBuf[REQ_CODE_LEN + 1] = "",
            pipeBuf[PIPE_BUF_LEN] = "",
            flagBuf[2] = "", // holds the flag for `openFile`
            argBuf[METADATA_SIZE + 1] = ""; // holds the number of files to read for `readNFiles`

        char
            * recvLine1,
            * recvLine2,
            * sendLine;

        struct fdNode* notifyList = NULL;
        FileNode_t* evictedList = NULL;

        // get ready fd from task queue
        DIE_ON_NEG_ONE(dequeue(taskBuf, (void*)&rdy_fd, sizeof(rdy_fd)));
        if (!rdy_fd) {
            break; // termination message
        }

        // read request code
        DIE_ON_NEG_ONE((numRead = read(rdy_fd, requestCodeBuf, REQ_CODE_LEN)));
        printf("%s ", requestCodeBuf);

        if (numRead) {
            long requestCode = atol(requestCodeBuf);
            //printf("read %s - reqn %ld\n", requestCodeBuf, requestCode);

            // get request filepath from client
            if (requestCode != READ_N_FILES) {
                recvLine1 = getRequestPayloadSegment(rdy_fd);
            }

            switch (requestCode) {
            case OPEN_FILE:
                printf("open %s\n", recvLine1);
                DIE_ON_NEG_ONE(read(rdy_fd, flagBuf, 1));
                long flags;
                if (isNumber(flagBuf, &flags) != 0) { // not a valid flag
                    puts("BAD FLAG");
                    SEND_RESPONSE_CODE(rdy_fd, BAD_REQUEST);
                }
                else {
                    if (openFileHandler(store, recvLine1, flags, &notifyList, rdy_fd) == -1) {
                        HANDLE_REQ_ERROR(rdy_fd);
                    }
                    else {
                        SEND_RESPONSE_CODE(rdy_fd, OK);
                        // if there were clients waiting to acquire lock on the deleted file(s) notify them
                        //that the file(s) don't exist (anymore)
                        NOTIFY_PENDING_CLIENTS(notifyList, FILE_NOT_FOUND, pipeBuf, pipeOut);
                    }
                }
                break;
            case CLOSE_FILE:
                puts("close");
                if (closeFileHandler(store, recvLine1, rdy_fd) == -1) {
                    HANDLE_REQ_ERROR(rdy_fd);
                }
                else {
                    SEND_RESPONSE_CODE(rdy_fd, OK);
                }
                break;
            case READ_FILE:
                ;
                char* outBuf;
                size_t readSize;
                puts("read");
                printf("client wants to read %s\n", recvLine1);
                if (readFileHandler(store, recvLine1, (void**)&outBuf, &readSize, rdy_fd) == -1) {
                    HANDLE_REQ_ERROR(rdy_fd);
                }
                else {
                    SEND_RESPONSE_CODE(rdy_fd, OK);
                    // send file content's length and file content
                    DIE_ON_NULL((sendLine = calloc(METADATA_SIZE + readSize + 1, 1)));
                    snprintf(sendLine, METADATA_SIZE + 1, "%08ld", readSize);
                    // we're not putting the "+ 1" after `METADATA_SIZE` because we want to overwrite the '\0' by `snprintf`
                    memcpy(sendLine + METADATA_SIZE, outBuf, readSize);
                    DIE_ON_NEG_ONE(writen(rdy_fd, sendLine, METADATA_SIZE + readSize));

                    free(sendLine);
                    free(outBuf);
                }
                break;
            case READ_N_FILES:
                puts("READ N FILE");
                DIE_ON_NEG_ONE(read(rdy_fd, argBuf, METADATA_SIZE));
                long upperLimit;
                if (isNumber(argBuf, &upperLimit) != 0) { // not a valid flag
                    SEND_RESPONSE_CODE(rdy_fd, BAD_REQUEST);
                }
                char* res;
                size_t resSize;
                readNFilesHandler(store, upperLimit, (void**)&res, &resSize);
                SEND_RESPONSE_CODE(rdy_fd, OK);
                DIE_ON_NEG_ONE(writen(rdy_fd, res, resSize));
                DIE_ON_NEG_ONE(writen(rdy_fd, NO_MORE_CONTENT, strlen(NO_MORE_CONTENT)));
                break;
            case WRITE_FILE:
                puts("write");
                // check that the last operation was `openFile` with `O_LOCK|O_CREATE`
                if (!testFirstWrite(store, recvLine1, rdy_fd)) {
                    SEND_RESPONSE_CODE(rdy_fd, FORBIDDEN);
                    // throw away the rest of the request payload
                    recvLine2 = getRequestPayloadSegment(rdy_fd);
                    free(recvLine2);
                    break;
                }
                // the logic of the `writeFile` operation is the same as that of `appendToFile` minus the initial check
            case APPEND_TO_FILE:
                puts("append");
                // get content to write/append
                recvLine2 = getRequestPayloadSegment(rdy_fd);
                if (writeToFileHandler(store, recvLine1, recvLine2, &notifyList, &evictedList, rdy_fd) == -1) {
                    HANDLE_REQ_ERROR(rdy_fd);
                }
                else {
                    SEND_RESPONSE_CODE(rdy_fd, OK);
                    // if there were clients waiting to acquire lock on the deleted file(s),
                    // notify them that the file(s) don't exist (anymore)
                    NOTIFY_PENDING_CLIENTS(notifyList, FILE_NOT_FOUND, pipeBuf, pipeOut);
                    char evictedBuf[10000] = "";
                    // send evicted files to client
                    while (evictedList) {
                        printf("EVICTING %s\n", evictedList->pathname);
                        FileNode_t* tmpPtr = evictedList;
                        SEND_EVICTED_FILE(rdy_fd, evictedList);
                        evictedList = evictedList->nextPtr;
                        deallocFile(tmpPtr);
                    }
                    // tell the client there are no more evicted files to read
                    sprintf(evictedBuf, NO_MORE_CONTENT);
                    DIE_ON_NEG_ONE(writen(rdy_fd, evictedBuf, strlen(NO_MORE_CONTENT)));
                }
                free(recvLine2);
                break;
            case LOCK_FILE:
                puts("lock");
                ;
                int outcome = lockFileHandler(store, recvLine1, rdy_fd);
                if (outcome == -1) {
                    HANDLE_REQ_ERROR(rdy_fd);
                }
                else if (outcome == -2) {
                    // client has to wait in order to acquire the lock: don't send any response for now
                    putFdBack = false;
                }
                else {
                    SEND_RESPONSE_CODE(rdy_fd, OK);
                }
                break;
            case UNLOCK_FILE:
                if (unlockFileHandler(store, recvLine1, &newLock, rdy_fd) == -1) {
                    HANDLE_REQ_ERROR(rdy_fd);
                }
                else {
                    SEND_RESPONSE_CODE(rdy_fd, OK);
                    if (newLock) { // another client that was waiting on this file finally acquired the lock on it
                        SEND_RESPONSE_CODE(newLock, OK);
                        // convert int to string
                        snprintf(pipeBuf, PIPE_BUF_LEN, "%04d", newLock);
                        // tell manager we're done handling the request of that client
                        DIE_ON_NEG_ONE(write(pipeOut, pipeBuf, PIPE_BUF_LEN));
                    }
                }
                puts("unlock");
                break;
            case REMOVE_FILE:
                puts("remove");
                if (removeFileHandler(store, recvLine1, &notifyList, rdy_fd) == -1) {
                    HANDLE_REQ_ERROR(rdy_fd);
                }
                else {
                    SEND_RESPONSE_CODE(rdy_fd, OK);
                    // if there were clients waiting to acquire lock on the deleted file
                    // notify them that the file doesn't exist (anymore)
                    NOTIFY_PENDING_CLIENTS(notifyList, FILE_NOT_FOUND, pipeBuf, pipeOut);
                }
                break;
            default:
                puts("\nUNKNOWN");
                printf("\n\n---\n- %ld\n- %s\n\n-----\n", requestCode, codeBuf);
                SEND_RESPONSE_CODE(rdy_fd, BAD_REQUEST);
            }
            // puts("Store:");
            // printStore(store);
            // puts("Affected file:");
            //printFile(store, recvLine1);

            // free the resources allocated to handle the request
            if (requestCode != READ_N_FILES) {
                free(recvLine1);
            }
            if (putFdBack) { // we're done handling this request - tell manager to put fd back in readset
                //printf("putting back %d\n", rdy_fd);
                // convert int to string
                snprintf(pipeBuf, PIPE_BUF_LEN, "%04d", rdy_fd);
                // tell manager we're done handling the request
                DIE_ON_NEG_ONE(write(pipeOut, pipeBuf, PIPE_BUF_LEN));
            }
        }
        else {
            puts("client left");
            // releases lock from all files the client had locked, and gets list of all clients that were "first in line" waiting to lock the file(s)
            DIE_ON_NEG_ONE(clientExitHandler(store, &notifyList, rdy_fd));
            // if the client had locked one or more files, and any of them had other clients blocked waiting to acquire the lock,
            // notify them that the operation has been completed successfully (they acquired the lock)
            NOTIFY_PENDING_CLIENTS(notifyList, OK, pipeBuf, pipeOut);

            updateClientCount(-1);
            DIE_ON_NEG_ONE(write(pipeOut, CLIENT_LEFT_MSG, PIPE_BUF_LEN)); // when the manager reads "0", he'll know a client left
        }
    }
    return NULL;
}


int main(int argc, char** argv) {
    /*
    Starts a server with a pool of `poolSize` worker threads
    */
    if (argc != 2) {
        fprintf(stderr, "Usage: ./server pathToConfigFile\n");
        return EXIT_FAILURE;
    }
    puts("starting server");
    Parser* configParser;
    DIE_ON_NULL((configParser = parseFile(argv[1], "=")));

    if (parserTestErr(configParser)) {
        fprintf(stderr, "Error parsing the config file: ");
        printErrAsStr(configParser);
        return EXIT_FAILURE;
    }


    ssize_t
        maxStorageCap,
        maxFileCount,
        workerPoolSize,
        taskBufSize,
        logBufSize,
        socketBacklog,
        replacementAlgo;

    char
        sockname[BUFSIZ],
        logfilename[BUFSIZ];

    GET_LONGVAL_OR_EXIT(configParser, "MAXSTORAGECAP", maxStorageCap, DFL_MAXSTORAGECAP, <= 0);
    GET_LONGVAL_OR_EXIT(configParser, "MAXFILECOUNT", maxFileCount, DFL_MAXFILECOUNT, <= 0);
    GET_LONGVAL_OR_EXIT(configParser, "WORKERPOOLSIZE", workerPoolSize, DFL_POOLSIZE, <= 0);
    GET_LONGVAL_OR_EXIT(configParser, "SOCKETBACKLOG", socketBacklog, DFL_SOCKETBACKLOG, <= 0);
    GET_LONGVAL_OR_EXIT(configParser, "TASKBUFSIZE", taskBufSize, DFL_TASKBUFSIZE, <= 1);
    GET_LONGVAL_OR_EXIT(configParser, "LOGBUFSIZE", logBufSize, DFL_LOGBUFSIZE, <= 1);
    GET_LONGVAL_OR_EXIT(configParser, "REPLACEMENTALGO", replacementAlgo, DFL_REPLACEMENTALGO, < FIFO_ALGO);
    GET_VAL_OR_EXIT(configParser, "SOCKETFILENAME", sockname, DFL_SOCKNAME);
    GET_VAL_OR_EXIT(configParser, "LOGFILENAME", logfilename, DFL_LOGFILENAME);

    destroyParser(configParser);

    struct sigaction sig_handler;
    memset(&sig_handler, 0, sizeof(sig_handler));
    sig_handler.sa_handler = exitSigHandler;
    sigset_t handlerMask;

    sigemptyset(&handlerMask);
    sigaddset(&handlerMask, SIGINT);
    sigaddset(&handlerMask, SIGHUP);
    sigaddset(&handlerMask, SIGTSTP);
    /*     // block signals while we're installing handler
        DIE_ON_NEG_ONE(sigprocmask(SIG_BLOCK, &handlerMask, &oldMask));
     */
    sig_handler.sa_mask = handlerMask;

    DIE_ON_NEG_ONE(sigaction(SIGINT, &sig_handler, NULL));
    DIE_ON_NEG_ONE(sigaction(SIGHUP, &sig_handler, NULL));
    DIE_ON_NEG_ONE(sigaction(SIGTSTP, &sig_handler, NULL));
    // ! remove this
    atexit(unlinkSock);


    BoundedBuffer* taskBuffer; // used by manager thread to pass incoming requests to workers
    CacheStorage_t* store; // in-memory file storage system

    pthread_t* workers; // pool of worker threads
    pthread_t logTid; // thread that writes logs to file

    int fd_socket,
        fd_communication;
    int fd_num = 0;

    int w2mPipe[2]; // worker-to-manager pipe to pass back fd's ready to be `select`ed again
    char pipebuf[PIPE_BUF_LEN] = "";

    struct sockaddr_un saddr; // contains the socket address

    fd_set
        rset,    // read set
        setsave; // copy of the original set for re-initialization


    DIE_ON_NULL((store = allocStorage(maxFileCount, maxStorageCap, replacementAlgo)));
    DIE_ON_NULL((taskBuffer = allocBoundedBuffer(MAX_TASKS, sizeof(int))));
    DIE_ON_NEG_ONE(pipe(w2mPipe)); // open worker-to-manager pipe

    strncpy(saddr.sun_path, sockname, UNIX_PATH_MAX);
    saddr.sun_family = AF_UNIX;
    puts(sockname);
    DIE_ON_NEG_ONE((fd_socket = socket(AF_UNIX, SOCK_STREAM, 0)));
    DIE_ON_NEG_ONE(bind(fd_socket, (struct sockaddr*)&saddr, sizeof saddr));
    DIE_ON_NEG_ONE(listen(fd_socket, MAX_CONN));

    puts("listening");
    printf("initial number of clients %zu\n", GET_CLIENT_COUNT);

    fd_num = MAX(fd_socket, fd_num);

    // initialize readset
    FD_ZERO(&setsave);
    FD_SET(fd_socket, &setsave);
    FD_SET(w2mPipe[0], &setsave);

    struct workerArgs* threadArgs = malloc(sizeof(*threadArgs));
    threadArgs->buf = taskBuffer;
    threadArgs->store = store;
    threadArgs->pipeOut = w2mPipe[1];

    struct logFlusherArgs logArgs = { .store = store };
    strncpy(logArgs.pathname, logfilename, MAX_LOG_PATHNAME);
    DIE_ON_NZ(pthread_create(&logTid, NULL, logFlusher, (void*)&logArgs));

    DIE_ON_NULL((workers = malloc(workerPoolSize * sizeof(pthread_t))));
    // create worker threads
    for (size_t i = 0; i < workerPoolSize; i++) {
        DIE_ON_NEG_ONE(pthread_create(&workers[i], NULL, &_startWorker, (void*)threadArgs));
    }


    while (!hardExit) {
        rset = setsave; // re-initialize the read set

        // wait for a fd to be ready for read operation
        if ((select(fd_num + 1, &rset, NULL, NULL, NULL)) == -1) {
            if (errno == EINTR) {
                if (softExit && GET_CLIENT_COUNT == 0) {
                    //goto cleanup;
                    break;
                }
                continue;
            }
            else {
                perror("select");
                return EXIT_FAILURE;
            }
        }

        // loop through the file descriptors
        for (size_t i = 0; i < fd_num + 1; i++) {
            if (FD_ISSET(i, &rset)) { // file descriptor is ready
                if (i == w2mPipe[0]) { // worker is done with a request
                    // add file descriptor back into readset
                    DIE_ON_NEG_ONE(read(i, pipebuf, PIPE_BUF_LEN));

                    //! remove after debug
                    if (atol(pipebuf) == 0) {
                        printf("client exited: curr num of clients %zu\n", GET_CLIENT_COUNT);
                    }
                    //! ---
                    if (atol(pipebuf) != 0) {
                        FD_SET(atol(pipebuf), &setsave);
                        fd_num = MAX(atol(pipebuf), fd_num);
                    }
                    else if (softExit && GET_CLIENT_COUNT == 0) { // reding 0 from pipe means a client left
                        goto cleanup; // using `goto` to break out of nested loops
                    }
                }
                else if (i == fd_socket) { // first request from a new client
                    puts("new client connected");
                    DIE_ON_NEG_ONE((fd_communication = accept(fd_socket, NULL, 0))); // accept incoming connection

                    if (softExit) { // reject connection immediately if we're soft exiting the server
                        DIE_ON_NEG_ONE(close(fd_communication));
                        puts("rejected connection because we're soft exiting");
                    }
                    else {
                        FD_SET(fd_communication, &setsave);
                        updateClientCount(1);
                        printf("number of clients %zu\n", GET_CLIENT_COUNT);

                        fd_num = MAX(fd_communication, fd_num);
                    }
                }
                else { // new request from already connected client
                    //puts("new request");
                    void* fd = &i;
                    FD_CLR(i, &setsave);
                    if (i == fd_num) {
                        fd_num--;
                    }
                    // push ready file descriptor to task queue for workers
                    DIE_ON_NEG_ONE(enqueue(taskBuffer, fd, 0));
                }
            }
        }
    }
cleanup:
    puts("cleanup");
    ;
    // send termination message(s) to workers
    int term = 0;
    for (size_t i = 0; i < workerPoolSize; i++) {
        DIE_ON_NEG_ONE(enqueue(taskBuffer, (void*)&term, 0));
    }
    // send termination message to log thread
    DIE_ON_NEG_ONE(enqueue(store->logBuffer, "EXIT", strlen("EXIT")));

    // wait for all threads to die
    for (size_t i = 0; i < workerPoolSize; i++) {
        DIE_ON_NEG_ONE(pthread_join(workers[i], NULL));
    }
    DIE_ON_NEG_ONE(pthread_join(logTid, NULL));

    DIE_ON_NEG_ONE(unlink(sockname));
    DIE_ON_NEG_ONE(close(w2mPipe[0]));
    DIE_ON_NEG_ONE(close(w2mPipe[1]));

    // we can access the thread without locking any mutex because we're the only thread left standing
    printf(
        STAT_MSG,
        store->maxReachedFileNum,
        store->maxReachedStorageSize,
        store->numVictims,
        store->currFileNum
    );

    // release resources on the heap
    destroyBoundedBuffer(taskBuffer);
    destroyStorage(store);
    free(threadArgs);
    free(workers);
}
