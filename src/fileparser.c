/*! \file */


#include "../include/fileparser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "../utils/scerrhand.h"
#include <assert.h>



struct _pair {
    /**
     * @brief A key:value pair
     **/
    char key[MAX_KEY_LEN + 1];
    char val[MAX_VAL_LEN + 1];
    struct _pair* nextPtr;
};

struct _parser {
    /**
     * @brief A parser for key:value files.
     * Supported file must adhere to this format: each line contains one and only one k:v pair,
     * and the delimitator needs to be the same for all lines: `key<delim>value`
     **/
    char filename[MAX_FILENAME_LEN + 1];
    char delim[MAX_DELIM_LEN + 1]; /*< A string used to separate a key from its value */
    struct _pair* hPtr;
    int err; /*< Contains zero if no error occurred during parsing; for the meaning of the error codes see function `parserTestErr` */
};

static void push(struct _pair** hPtr, struct _pair* newPair) {
    newPair->nextPtr = *hPtr;
    *hPtr = newPair;
}

static int parse(Parser* p, FILE* fp) {
    char buf[BUFSIZ];
    struct _pair* kvPair;
    size_t i = 0;
    while (i++, fgets(buf, BUFSIZ, fp)) {
        char* savePtr, * token;
        kvPair = calloc(sizeof(*kvPair), 1);
        if (!kvPair) {
            return -3; // out of memory
        }

        // todo check if a line begins with '#' for comments or is an empty line

        // get key
        token = strtok_r(buf, p->delim, &savePtr);
        strncpy(kvPair->key, token, sizeof kvPair->key);
        // nul-terminate the string in case the string we copied wasn't itself nul-terminated
        kvPair->key[sizeof kvPair->key - 1] = '\0';

        // remove any trailing spaces after the key name
        kvPair->key[strcspn(kvPair->key, " ")] = '\0';

        // get value
        token = strtok_r(NULL, p->delim, &savePtr);
        if (!token) { // delim missing: invalid syntax
            free(kvPair);
            return i;
        }
        strncpy(kvPair->val, token, strlen(token));
        // remove trailing newline
        kvPair->val[strcspn(kvPair->val, "\n")] = '\0';
        push(&p->hPtr, kvPair);
    }
    return 0;
}

Parser* parseFile(char* filename, char* delim) {
    /**
     * @brief Loads the key:value pairs from specified file into memory
     *
     * @param filename Name of the file to parse
     * @param delim Delimitator for key:value pairs
     *
     * @return A pointer to a newly allocated `Parser` containing the parsed data (see `parserTestErr`)
     * @return NULL if memory for the parser could not be allocated
    **/
    FILE* fp;
    Parser* p = calloc(sizeof(Parser), 1);

    if (!p) {
        return NULL;
    }
    strncpy(p->filename, filename, MAX_FILENAME_LEN);
    strncpy(p->delim, delim, MAX_DELIM_LEN);

    if ((fp = fopen(filename, "r")) == NULL) {
        p->err = -2;
    }
    else {
        // parse file and capture error code
        p->err = parse(p, fp);
        fclose(fp);
    }

    return p;
}

int parserTestErr(Parser* p) {
    /**
     * @brief Returns an int describing an error that occurred during the use of `p`
     *
     * @param p a pointer to the parser to test for errors
     *
     * @return 0 if no error occurred, otherwise see below
     *
     *  Error codes:\n
     *  -1: error with the use of this function itself (check `errno`)\n
     *  -2: requested file could not be opened (fopen error)\n
     *  -3: memory allocation for one of the k:v pairs failed (malloc error)\n
     *  a positive integer `j`: syntax error on line `j` (parsing error)
     */
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    return p->err;
}

int printErrAsStr(Parser* p) {
    /**
     * @brief Prints a description of the error in `p->err`
     *
     * @param p A pointer to the parser whose error we want to print.
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * Upon error, `errno` will have value:\n
     * `EINVAL` for invalid parameter(s)
     */

    if (!p) {
        errno = EINVAL;
        return -1;
    }

    if (!p->err) {
        puts("No error(s) detected.");
        return 0;
    }

    switch (p->err) {
    case -2:
        fprintf(stderr, "Error opening file.\n");
        break;
    case -3:
        fprintf(stderr, "Memory allocation error.\n");
        break;
    default:
        fprintf(stderr, "Syntax error on line %d.\n", p->err);
    }
    return 0;
}

int getValueFor(Parser* p, const char* key, char* dest, const char* defaultVal) {
    /**
     * @brief Attempts to get the value associated with the given key.
     *
     * @param p A pointer to the parser from which we want to get the value.
     * @param key A string containing the key name
     * @param dest The pointer to a buffer to which the value is to be copied
     * @param defaultVal A string containing the default value we want to get if the requested key cannot be found in the parser
     *
     * @return 1 if the key was found in the parser
     * @return 0 if no such key was found and the supplied default value was used
     * @return -1 if an error occurred (sets `errno`)
     *
     * At the end of execution, param `dest` will contain either the `defaultVal` or the value associated with `key`.\n
     * \n
     * Upon error, `errno' will have the following value:\n
     * `EINVAL` for invalid parameter(s)
     *
     */
    if (!p || !dest) {
        errno = EINVAL;
        return -1;
    }

    struct _pair*
        currPtr = p->hPtr;

    while (currPtr && strcmp(key, currPtr->key)) {
        currPtr = currPtr->nextPtr;
    }

    // copy value if key is found; otherwise copy default value
    strncpy(dest, currPtr ? currPtr->val : defaultVal, MAX_VAL_LEN);

    return currPtr ? 1 : 0;
}

long getLongValueFor(Parser* p, const char* key, long defaultVal) {
    /**
     * @brief Utility function to get a `long` value for a key
     *
     * @param p A pointer to the parser from which we want to get the value.
     * @param key A string containing the key name
     * @param defaultVal A string containing the default value we want to get if the requested key cannot be found in the parser
     *
     * @return The value associated with `key` (cast to `long`) if such a value exists and is a valid `long`
     * @return `defaultVal` if the key couldn't be found
     * @return -1 if an error occurred (sets `errno`)
     *
     * Upon error, `errno` will contain one of the following values:
     * `EINVAL` if the requested key was associated with a non-`long` value or if invalid parameter(s) were passed
     * `ERANGE` if the requested value caused overflow or underflow (see `strtol`'s documentation)
     **/
    char buf[MAX_VAL_LEN], * end = NULL;
    int res = getValueFor(p, key, buf, "");

    if (!res) {
        return defaultVal;
    }
    else if (res == -1) {
        return -1;
    }

    errno = 0;
    long lval = strtol(buf, &end, 0);

    if (!errno && end == buf) {
        // no valid `long` found
        errno = EINVAL;
    }
    return errno ? -1 : lval;
}

int destroyParser(Parser* p) {
    /**
     * @brief Frees every key:value pair in the parser, than frees the parser.
     *
     * @param p A pointer to the parser to free.
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * Upon error, `errno` will have the value:\n
     * `EINVAL` for invalid parameter(s)
     */
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    struct _pair* tmp;
    while (p->hPtr) {
        tmp = p->hPtr;
        p->hPtr = p->hPtr->nextPtr;
        free(tmp);
    }
    free(p);
    return 0;
}
