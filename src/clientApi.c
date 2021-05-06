#include "../include/clientApi.h"
#include "../include/requests.h"
#include "../utils/scerrhand.h"
#include <unistd.h>

int openConnection(const char* sockname, int msec, const struct timespec abstime);
int closeConnection(const char* sockname);
int openFile(const char* pathname, int flags);
int readFile(const char* pathname, void** buf, size_t* size);
int writeFile(const char* pathname, const char* dirname);
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);
int lockFile(const char* pathname) {
    ClientRequest_t req = { "LOCK", pathname, -1 };

}
int unlockFile(const char* pathname);
int closeFile(const char* pathname);
int removeFile(const char* pathname);