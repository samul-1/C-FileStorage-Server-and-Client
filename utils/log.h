#define WRITE_EXTOP_TO_LOG_BUF(buf, op, fp, size, requestor) \
sprintf(buf+strlen(buf), "REQ: %d WO: %ld - %s %s (%zu bytes)\n", requestor, pthread_self(), #op, fp->pathname, size);