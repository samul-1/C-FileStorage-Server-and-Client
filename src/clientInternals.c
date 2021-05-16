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
    //puts(dir);
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
    if (mkdir(tmp, S_IRWXU) == -1) {
        return -1;
    }
    return 0;
}

int saveFileToDisk(char* filepath, char* filecontent, size_t filecontentsize) {
    // split file name from rest of path and recursively create the directories
    char* lastSlash = strrchr(filepath, '/');
    if (lastSlash) {
        *lastSlash = '\0';
        // just pass the directories without the filename
        if (_mkdir(filepath) == -1) {
            int errnosave = errno;
            perror("mkdir");
            free(filepath);
            errno = errnosave;
            return -1;
        }
        *lastSlash = '/';
    }

    // now save file to disk
    FILE* fp = fopen(filepath, "w+");
    if (fp == NULL) {
        return -1;
    }
    if (fwrite(filecontent, 1, filecontentsize, fp) <= 0) {
        int errnosave = errno;
        if (ferror(fp)) {
            errno = errnosave;
            return -1;
        }
    }
    fclose(fp);
    return 0;
}