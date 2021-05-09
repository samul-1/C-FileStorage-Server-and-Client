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
#include "../include/boundedbuffer.h"
#include "../include/fileparser.h"
#include "../utils/scerrhand.h"
#include "../include/filesystemApi.h"

#define UNIX_PATH_MAX 108
#define MAX_CONN 10
#define MAX_MSG_LEN 6094
#define MAX(a,b) (a) > (b) ? (a) : (b)
#define MAX_TASKS 2048

#define DFL_POOLSIZE 10
#define DFL_MAXSTORAGECAP 10000
#define DFL_MAXFILECOUNT 100
#define DFL_SOCKNAME "tmp/serversocket.sk"
#define DFL_LOGFILENAME "logs.txt"
#define DFL_SOCKETBACKLOG 10
#define DFL_TASKBUFSIZE 2048
#define DFL_LOGBUFSIZE 2048
#define DFL_REPLACEMENTALGO 0

#define GET_LD_OR_EXIT(p, k, v, d)\
    if((v = getLongValueFor(p, k, d)) == -1) {\
    perror("Error getting value for " #k);\
    if(p) {\
        destroyParser(p);\
    }\
    exit(EXIT_FAILURE);\
}

#define GET_VAL_OR_EXIT(p, k, b, d)\
    if(getValueFor(p, k, b, d) == -1) {\
    perror("Error getting value for " #k);\
    if(p) {\
        destroyParser(p);\
    }\
    exit(EXIT_FAILURE);\
}

struct _args {
    BoundedBuffer* buf;
    CacheStorage_t* store;
    int pipeOut;
};


void* _startWorker(void* args) {
    /*
    Upon being called, enters an infinite loop and
    reads tasks from the queue, processing them one at a time
    */
    while (1) {
        int rdy_fd;
        ssize_t numRead;
        char buf[MAX_MSG_LEN];
        char pipeBuf[5];

        BoundedBuffer* taskBuf = ((struct _args*)args)->buf;
        int pipeOut = ((struct _args*)args)->pipeOut;

        // get ready fd from task queue
        dequeue(taskBuf, (void*)&rdy_fd, sizeof(rdy_fd));

        DIE_ON_NEG_ONE((numRead = read(rdy_fd, buf, MAX_MSG_LEN)));

        if (numRead) {
            DIE_ON_NEG_ONE(write(rdy_fd, "hey", 4));
            sprintf(pipeBuf, "%d", rdy_fd); // convert int to string
            DIE_ON_NEG_ONE(write(pipeOut, pipeBuf, 5)); // tell manager we're done handling the request
        }
        else {
            puts("client left");
        }
    }
}

int main(int argc, char** argv) {
    /*
    Starts a server with a pool of `poolSize` worker threads
    */
    Parser* configParser;
    DIE_ON_NULL(configParser = parseFile("config.txt", "="));

    if (parserTestErr(configParser)) {
        printErrAsStr(configParser);
        return EXIT_FAILURE;
    }

    size_t
        maxStorageCap,
        maxFileCount,
        workerPoolSize,
        taskBufSize,
        logBufSize,
        socketBacklog,
        replacementAlgo;

    char
        valBuf[MAX_VAL_LEN + 1], // holds value parsed from config file
        sockname[BUFSIZ],
        logfilename[BUFSIZ];

    // todo make variadic macro for this
    GET_LD_OR_EXIT(configParser, "MAXSTORAGECAP", maxStorageCap, DFL_MAXSTORAGECAP);
    GET_LD_OR_EXIT(configParser, "MAXFILECOUNT", maxFileCount, DFL_MAXFILECOUNT);
    GET_LD_OR_EXIT(configParser, "WORKERPOOLSIZE", workerPoolSize, DFL_POOLSIZE);
    GET_LD_OR_EXIT(configParser, "SOCKETBACKLOG", socketBacklog, DFL_SOCKETBACKLOG);
    GET_LD_OR_EXIT(configParser, "TASKBUFSIZE", taskBufSize, DFL_TASKBUFSIZE);
    GET_LD_OR_EXIT(configParser, "LOGBUFSIZE", logBufSize, DFL_LOGBUFSIZE);
    GET_LD_OR_EXIT(configParser, "REPLACEMENTALGO", replacementAlgo, DFL_REPLACEMENTALGO);
    GET_VAL_OR_EXIT(configParser, "SOCKETFILENAME", sockname, DFL_SOCKNAME);
    GET_VAL_OR_EXIT(configParser, "LOGFILENAME", logfilename, DFL_LOGBUFSIZE);


    BoundedBuffer* taskBuffer; // used by manager thread to pass incoming requests to workers
    CacheStorage_t* store; // in-memory file storage system

    pthread_t* workers; // pool of worker threads

    DIE_ON_NULL(workers = malloc(workerPoolSize * sizeof(pthread_t)));


    int fd_socket,
        fd_communication;
    int fd_num = 0;

    int w2mPipe[2]; // worker-to-manager pipe to pass back fd's ready to be `select`ed again
    char pipebuf[BUFSIZ];

    struct sockaddr_un saddr; // contains the socket address

    fd_set
        rset,    // read set
        setsave; // copy of the original set for re-initialization

    DIE_ON_NULL((store = allocStorage(maxFileCount, maxStorageCap, replacementAlgo)));
    DIE_ON_NULL((taskBuffer = allocBoundedBuffer(MAX_TASKS, sizeof(int))));
    DIE_ON_NEG_ONE(pipe(w2mPipe)); // open worker-to-manager pipe

    strncpy(saddr.sun_path, sockname, UNIX_PATH_MAX);
    saddr.sun_family = AF_UNIX;

    DIE_ON_NEG_ONE((fd_socket = socket(AF_UNIX, SOCK_STREAM, 0)));
    DIE_ON_NEG_ONE(bind(fd_socket, (struct sockaddr*)&saddr, sizeof saddr));
    DIE_ON_NEG_ONE(listen(fd_socket, MAX_CONN));

    //puts("listening");

    fd_num = MAX(fd_socket, fd_num);

    // initialize readset mask
    FD_ZERO(&setsave);
    FD_SET(fd_socket, &setsave);
    FD_SET(w2mPipe[0], &setsave); // read messages from workers

    struct _args* threadArgs = malloc(sizeof(*threadArgs));
    threadArgs->buf = taskBuffer;
    threadArgs->store = store;
    threadArgs->pipeOut = w2mPipe[1];

    // create worker threads
    for (size_t i = 0; i < workerPoolSize; i++) {
        DIE_ON_NEG_ONE(pthread_create(&workers[i], NULL, &_startWorker, (void*)threadArgs));
    }

    while (1) {
        rset = setsave; // re-initialize the read set

        // wait for a fd to be ready for read operation
        DIE_ON_NEG_ONE(select(fd_num + 1, &rset, NULL, NULL, NULL));

        // loop through the file descriptors
        for (size_t i = 0; i < fd_num + 1; i++) {
            if (FD_ISSET(i, &rset)) { // file descriptor is ready
                if (i == w2mPipe[0]) { // worker is done with a request
                    // add file descriptor back into readset
                    DIE_ON_NEG_ONE(read(i, pipebuf, BUFSIZ));
                    FD_SET(atol(pipebuf), &setsave);
                    fd_num = MAX(atol(pipebuf), fd_num);
                }
                else if (i == fd_socket) { // first request from a new client
                    // todo if SIGINT was received, we need to reject the connection
                    puts("new client connected");

                    DIE_ON_NEG_ONE((fd_communication = accept(fd_socket, NULL, 0))); // accept incoming connection
                    FD_SET(fd_communication, &setsave);

                    fd_num = MAX(fd_communication, fd_num);
                }
                else { // new request from already connected client
                    void* fd = &i;
                    FD_CLR(i, &setsave);
                    if (i == fd_num) {
                        fd_num--;
                    }
                    // push ready file descriptor to task queue for workers
                    // todo check for errors
                    enqueue(taskBuffer, fd);
                }
            }
        }

    }
}
