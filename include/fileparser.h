#ifndef FILE_PARSER_H
#define FILE_PARSER_H

#define _GNU_SOURCE

#define MAX_FILENAME_LEN 500
#define MAX_DELIM_LEN 3
#define MAX_KEY_LEN 100
#define MAX_VAL_LEN 500

typedef struct _parser Parser;

Parser* parseFile(char* filename, char* delim);
int getValueFor(Parser* p, const char* key, char* dest, const char* defaultVal);
long getLongValueFor(Parser* p, const char* key, long defaultVal);
int parserTestErr(Parser* p);
int printErrAsStr(Parser* p);
int destroyParser(Parser* p);

#endif