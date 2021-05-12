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
    int fd_socket; // file descriptor of the sockets
    struct sockaddr_un saddr; // contains the socket address
    //char readBuf[MAX_MSG_LEN];
    char writeBuf[MAX_MSG_LEN];
    //pthread_t readThread; // thread that will execute the read and print of received messages

    strncpy(saddr.sun_path, SOCKNAME, UNIX_PATH_MAX); // set socket address
    saddr.sun_family = AF_UNIX; // set socket family


    fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    while (connect(fd_socket, (struct sockaddr*)&saddr, sizeof saddr) == -1) {
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
    // a->socketFd = fd_socket;
    // a->buf = readBuf;
    // a->bufsize = MAX_MSG_LEN;

    // create thread to read and print out received messages
    //pthread_create(&readThread, NULL, listenForMessages, a);

    while (1) {
        memset(writeBuf, 0, MAX_MSG_LEN);
        //scanf("%s", writeBuf); // type next message
        char sendBuf[MAX_MSG_LEN * 2] = "";
        //char flagbuf[2] = "";

        fgets(writeBuf, MAX_MSG_LEN, stdin);
        if (!strncmp(writeBuf, "exit", 4)) { // close on "exit" message
            break;
        }
        else if (!strncmp(writeBuf, "open", 4)) {
            sendBuf[0] = '0' + OPEN_FILE;
            puts(sendBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));

            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s%d", strlen(writeBuf), writeBuf, 1);
            puts(sendBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));
        }
        else if (!strncmp(writeBuf, "write", 5)) {
            sendBuf[0] = '0' + WRITE_FILE;
            write(fd_socket, sendBuf, strlen(sendBuf));

            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s", strlen(writeBuf), writeBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));

            // content
            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s", strlen(writeBuf), writeBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));

        }
        else if (!strncmp(writeBuf, "lock", 4)) {
            sendBuf[0] = '0' + LOCK_FILE;
            write(fd_socket, sendBuf, strlen(sendBuf));

            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s", strlen(writeBuf), writeBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));
        }
        else if (!strncmp(writeBuf, "unlock", 6)) {
            sendBuf[0] = '0' + UNLOCK_FILE;
            write(fd_socket, sendBuf, strlen(sendBuf));

            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s", strlen(writeBuf), writeBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));
        }
        else if (!strncmp(writeBuf, "close", 5)) {
            sendBuf[0] = '0' + CLOSE_FILE;
            write(fd_socket, sendBuf, strlen(sendBuf));

            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s", strlen(writeBuf), writeBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));
        }
        else if (!strncmp(writeBuf, "read", 4)) {
            sendBuf[0] = '0' + READ_FILE;
            write(fd_socket, sendBuf, strlen(sendBuf));

            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s", strlen(writeBuf), writeBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));
        }
        else if (!strncmp(writeBuf, "remove", 6)) {
            sendBuf[0] = '0' + REMOVE_FILE;
            write(fd_socket, sendBuf, strlen(sendBuf));

            memset(sendBuf, 0, 2 * MAX_MSG_LEN);

            // name
            fgets(writeBuf, MAX_MSG_LEN, stdin);
            writeBuf[strlen(writeBuf) - 1] = '\0';
            sprintf(sendBuf, "%08ld%s", strlen(writeBuf), writeBuf);
            write(fd_socket, sendBuf, strlen(sendBuf));
        }



        // else {

        //     puts("you typed:");
        //     puts(writeBuf);
        //     size_t len = strlen(writeBuf);
        //     char tmp[10] = "";
        //     sprintf(tmp, "3%08zu", len - 1);
        //     char sendBuf[MAX_MSG_LEN] = "";
        //     strncpy(sendBuf, tmp, 9);
        //     strncpy((sendBuf + 9), writeBuf, strlen(writeBuf) - 1);
        //     puts("server gonna see:");
        //     puts(sendBuf);
        //     write(fd_socket, sendBuf, strlen(sendBuf)); // write to socket
        //     //write(fd_socket, "300000005abcde", 14); // write to socket
        // }
    }
    //pthread_kill(readThread, SIGINT); // end read thread
    close(fd_socket); // close communication
    exit(EXIT_SUCCESS);

}