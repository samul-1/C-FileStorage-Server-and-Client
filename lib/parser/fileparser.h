#ifndef FILE_PARSER_H
#define FILE_PARSER_H

#define _GNU_SOURCE

typedef struct _parser Parser;

Parser* parseFile(char* filename, char* delim);
int getValueFor(Parser* p, const char* key, char* dest, const char* defaultVal);
long getLongValueFor(Parser* p, const char* key, long defaultVal);
int parserTestErr(Parser* p);
int printErrAsStr(Parser* p);
int destroyParser(Parser* p);

#endif