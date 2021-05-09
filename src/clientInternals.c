#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include "../utils/scerrhand.h"
// TODO write docs

/*

The protocol looks like this:
`numFiles(5 digits),{foreach file}lengthPath(8 dig),filepath,lengthFileContent(8 dig),filecontent{end}`

For example, if the directory looks like this:

| file1.txt (contains "abc")
| subdir
    | file2.txt (contains "abdefg")

The message is gonna be:
`00002,0000009,file1.txt,00000003,abc,00000016,subdir/file2.txt,00000006,abcdefg`

*/

// 5 digits for the number of files, 1 comma, and '\0' (7)
#define DFL_MSG_SIZE (5+1+1)
// 8 digits for the filename length, 1 comma, 8 digits for the content size, 1 comma, and '\0' (19)
#define PER_FILE_MSG_SIZE (8+1+8+1+1)

#define FILE_AMOUNT_DIGITS 5
#define METADATA_SIZE_DIGITS 8


int getFilesContentInDir(char* directoryPath, size_t upperLimit, char** dest, size_t* finalMsgSize, size_t* readFiles, bool topLevel) {
    DIR* targetDir;
    FILE* currFileDesc;

    struct dirent* currFile;
    struct stat st;

    char
        * pathPrefix, // saves the given pathname to avoid modifying the passed parameter
        * filePathname; // contains each file's name preceded by the directory name

    size_t fileCount = 0; // files processed so far

    size_t msgSize = DFL_MSG_SIZE;

    char* msg; // will contain the constructed message

    if ((msg = calloc(msgSize, 1)) == NULL) {
        errno = ENOMEM;
        return -1;
    };


    // allocate string to store the prefix for files
    if ((pathPrefix = calloc((strlen(directoryPath) + 2), 1)) == NULL) {
        free(msg);
        errno = ENOMEM;
        return -1;
    };

    // no need to use `strncpy` because we allocated memory using the dest length anyway
    strcpy(pathPrefix, directoryPath);
    //pathPrefix[strlen(directoryPath) + 2] = '\0';
    // no need to use `strncat` when concatenating a literal
    strcat(pathPrefix, "/");
    DIE_ON_NULL((targetDir = opendir(directoryPath)));

    errno = 0;
    while ((currFile = readdir(targetDir)) && (!upperLimit || fileCount < upperLimit))
    {
        // skip current and parent dirs
        if (!strcmp(currFile->d_name, ".") || !strcmp(currFile->d_name, "..")) {
            continue;
        }

        // store "currDir/filename" string
        if ((filePathname = calloc((strlen(pathPrefix) + strlen(currFile->d_name) + 1), 1)) == NULL) {
            free(pathPrefix);
            free(msg);
            errno = ENOMEM;
            return -1;
        }
        strcpy(filePathname, pathPrefix);
        strcat(filePathname, currFile->d_name);

        if ((currFileDesc = fopen(filePathname, "r")) == NULL) {
            free(pathPrefix);
            free(msg);
            free(filePathname);
            perror("fopen");
            return -1;
        };

        size_t filePathLen = strlen(filePathname);

        // get length of file content
        if (stat(filePathname, &st) == -1) {
            free(pathPrefix);
            free(msg);
            free(filePathname);
            perror("stat");
            return -1;
        }

        size_t oldMsgLen = strlen(msg);

        if (S_ISDIR(st.st_mode)) {
            // recursively visit subdirectory
            char* recTmp; // output from recursive call
            size_t recWritten; // message length from recursive call
            size_t recCount; // number of files read in recursive call
            if (getFilesContentInDir(filePathname, (upperLimit ? upperLimit - fileCount : 0), &recTmp, &recWritten, &recCount, false) == -1) {
                free(pathPrefix);
                free(msg);
                free(filePathname);
                perror("recursive");
                return -1;
            }

            msgSize += recWritten;

            fileCount += recCount;

            void* tmp = realloc(msg, msgSize);
            if (!tmp) {
                free(msg);
                free(pathPrefix);
                free(filePathname);
                errno = ENOMEM;
                return -1;
            }
            // zero out memory to make sure we're working with the correct string length
            memset(tmp + oldMsgLen, 0, msgSize - oldMsgLen);
            msg = (char*)tmp;
            strncat(msg, recTmp, recWritten);
            free(recTmp);
        }
        else {
            fileCount += 1;
            size_t fileContentSize = st.st_size;

            // add space for the file path and content
            msgSize += PER_FILE_MSG_SIZE + filePathLen + fileContentSize;


            void* tmp = realloc(msg, msgSize);
            if (!tmp) {
                free(msg);
                free(pathPrefix);
                free(filePathname);
                errno = ENOMEM;
                return -1;
            }
            // we now have memory to hold "filenamelength,filename,fileContentLength,fileContent,"

            // zero out memory to make sure we're working with the correct string length
            memset(tmp + oldMsgLen, 0, msgSize - oldMsgLen);
            msg = (char*)tmp;

            snprintf(msg + strlen(msg), (PER_FILE_MSG_SIZE + filePathLen + fileContentSize), "%08zu,%s,%08zu,", filePathLen, filePathname, fileContentSize);

            // read content of the file and append it to msg buffer
            while (fgets(msg + strlen(msg), BUFSIZ, currFileDesc) != NULL) {
                ;
            }
            if (!feof(currFileDesc)) {
                free(filePathname);
                free(pathPrefix);
                free(msg);
                perror("feof");
                return -1;
            }
        }
        free(filePathname);

        // add trailing comma for next file
        msg[strlen(msg)] = ',';
        if (fclose(currFileDesc)) {
            free(pathPrefix);
            free(msg);
            perror("fclose");
            return -1;
        }

        // reset errno for next iteration (to check `readdir` errors)
        errno = 0;
    }

    if (errno) { // error with `readdir`
        return -1;
    }

    // now that we know how many files we're sending, print that number at the
    // beginning of the message
    if (topLevel) {
        snprintf(msg, FILE_AMOUNT_DIGITS + 1, "%05zu", fileCount); // +1 even though I'll overwrite the '\0' to suppress gcc warning

        // overwrite '\0' by `sprintf` with a comma as per the described protocol
        msg[5] = ',';

    }
    // remove last trailing comma from output
    *strrchr(msg, (int)',') = '\0';

    free(pathPrefix);
    if (closedir(targetDir) == -1) {
        perror("closedir");
        return -1;
    }
    msgSize = strlen(msg); // ? investigate
    *finalMsgSize = msgSize;
    *readFiles = fileCount;
    *dest = msg;
    return 0;
}
