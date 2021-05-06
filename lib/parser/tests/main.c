#include "../fileparser.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int main(void) {
    // Test battery 1 - intended behavior (no errors)

    Parser* p1 = parseFile("test1.txt", "=");
    long dest;
    char buf[BUFSIZ];

    // no error must occur with a valid file
    assert(!parserTestErr(p1));

    // get long value
    dest = getLongValueFor(p1, "A", -1);
    assert(!errno); // no error occurred
    assert(dest == 1); // correctly retrieved value

    // decimal values are truncated (aka `floor()`'d)
    dest = getLongValueFor(p1, "F", -1);
    assert(!errno); // no error occurred
    assert(dest == 2); // correctly retrieved integer part of value


    // get value for a key that's not in the file
    dest = getLongValueFor(p1, "Z", -1);
    assert(!errno); // no error occurred
    assert(dest == -1); // used default value

    // spaces after key name handled correctly
    dest = getLongValueFor(p1, "B", -1);
    assert(!errno); // no error occurred
    assert(dest == 2); // correctly retrieved value

    //get value as string
    getValueFor(p1, "C", buf, "default");
    assert(!strncmp(buf, "string", 7));

    // get default string value
    getValueFor(p1, "D", buf, "default");
    assert(!strncmp(buf, "default", 8));

    destroyParser(p1);
    puts("Battery 1 - All passed");

    // Test battery 2 - parsing errors

    Parser* p2 = parseFile("nonexistent.txt", "=");
    assert(parserTestErr(p2) == -2); // trying to parse a non-existent file gives error -2
    Parser* p3 = parseFile("test2.txt", "=");
    assert(parserTestErr(p3) == 2); // there's a syntax error on line 2

    destroyParser(p2);
    destroyParser(p3);
    puts("Battery 2 - All passed");

    // Test battery 3 - `getLongValueFor` errors

    Parser* p4 = parseFile("test3.txt", "=");
    dest = getLongValueFor(p4, "NOTALONG", 0);
    assert(errno == EINVAL); // -2 means the corresponding value is not a `long`
    assert(dest == -1); // -1 is returned upon encountering an error

    dest = getLongValueFor(p4, "WAYTOOBIG", 0);
    assert(errno == ERANGE); // -1 means overflow or underflow;
    assert(dest == -1); // -1 is returned upon encountering an error

    dest = getLongValueFor(p4, "WAYTOOSMALL", 0);
    assert(errno == ERANGE); // -1 means overflow or underflow;
    assert(dest == -1); // -1 is returned upon encountering an error

    destroyParser(p4);
    puts("Battery 3 - All passed");
}
