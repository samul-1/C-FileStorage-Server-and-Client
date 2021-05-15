#ifndef CLI_PARSER_H
#define CLI_PARSER_H

typedef struct cliOption {
    char option;
    char* argument;
    struct cliOption* nextPtr;
} CliOption;

CliOption* parseCli(int numStrings, char** strings);
CliOption* popOption(CliOption** optList, char optName);

int deallocParser(CliOption* list);
int deallocOption(CliOption* optPtr);
void printParser(CliOption* list);

#endif