#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include "../include/requestCode.h"
#include "../include/clientApi.h"
#include "../utils/flags.h"
#define SOCKNAME "serversocket.sk"
#define UNIX_PATH_MAX 108
#define MAX_CONN 2
#define MAX_MSG_LEN 1024

struct _args {
    int socketFd;
    char* buf;
    int bufsize;
};

void* listenForMessages(void* args) {
    struct _args* a = args;
    while (1) {
        if (read(a->socketFd, a->buf, a->bufsize)) {
            puts(a->buf);

        }
    }
}

int main(int argc, char** argv) {
    PRINTS_ENABLED = true;

    struct sockaddr_un saddr; // contains the socket address
    //char readBuf[MAX_MSG_LEN];
    char writeBuf[MAX_MSG_LEN];
    //pthread_t readThread; // thread that will execute the read and print of received messages

    strncpy(saddr.sun_path, SOCKNAME, UNIX_PATH_MAX); // set socket address
    saddr.sun_family = AF_UNIX; // set socket family


    SOCKET_FD = socket(AF_UNIX, SOCK_STREAM, 0);
    while (connect(SOCKET_FD, (struct sockaddr*)&saddr, sizeof saddr) == -1) {
        if (errno == ENOENT) { // socket not found
            puts("waiting...");
            sleep(1);
        }
        else { // other error
            perror("error: ");
            exit(EXIT_FAILURE);
        }
    }

    // set arguments for the readThread routine
    // struct _args* a = malloc(sizeof(struct _args*));
    // a->socketFd = SOCKET_FD;
    // a->buf = readBuf;
    // a->bufsize = MAX_MSG_LEN;

    // create thread to read and print out received messages
    //pthread_create(&readThread, NULL, listenForMessages, a);

    // write(SOCKET_FD, "200000002ab1200000002ab1200000007abcdefg1", strlen("200000002ab1200000002ab1200000007abcdefg1"));
    // sleep(10);


    while (1) {
        size_t sz;
        char* bf;
        memset(writeBuf, 0, MAX_MSG_LEN);
        //scanf("%s", writeBuf); // type next message
       // char sendBuf[MAX_MSG_LEN * 2] = "";
        //char flagbuf[2] = "";

        fgets(writeBuf, MAX_MSG_LEN, stdin);
        if (!strncmp(writeBuf, "exit", 4)) { // close on "exit" message
            break;
        }
        else if (!strncmp(writeBuf, "open", 4)) {
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            int flag;
            scanf("%d", &flag);
            openFile(writeBuf, flag);
        }
        else if (!strncmp(writeBuf, "append", 6)) {
            char write2buf[MAX_MSG_LEN];
            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';

            // content
            fgets(write2buf, MAX_MSG_LEN, stdin);
            write2buf[strlen(write2buf) - 1] = '\0';

            if (appendToFile(writeBuf, write2buf, strlen(write2buf), "downloadsAppend") == -1) {
                perror("append");
            }
        }
        else if (!strncmp(writeBuf, "write", 5)) {
            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';

            writeFile(writeBuf, "downloadsWrite");
        }
        else if (!strncmp(writeBuf, "lock", 4)) {
            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';

            lockFile(writeBuf);
        }
        else if (!strncmp(writeBuf, "unlock", 6)) {
            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';

            unlockFile(writeBuf);
        }
        else if (!strncmp(writeBuf, "close", 5)) {
            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';

            closeFile(writeBuf);
        }
        else if (!strncmp(writeBuf, "readn", 5)) {
            // name
            //fgets(writeBuf, MAX_MSG_LEN, stdin);
            //writeBuf[strlen(writeBuf) - 1] = '\0';
            int num;
            scanf("%d", &num);

            if (readNFiles(num, "downloadsReadN") == -1) {
                perror("readnfiles");
            }
        }
        else if (!strncmp(writeBuf, "read", 4)) {
            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';

            if (readFile(writeBuf, (void**)&bf, &sz) == 0) {
                printf("Size: %zu, Read: %s\n", sz, bf);
                free(bf);
            }
        }
        else if (!strncmp(writeBuf, "remove", 6)) {
            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';

            removeFile(writeBuf);
        }
    }
    //pthread_kill(readThread, SIGINT); // end read thread
    close(SOCKET_FD); // close communication
    exit(EXIT_SUCCESS);

}