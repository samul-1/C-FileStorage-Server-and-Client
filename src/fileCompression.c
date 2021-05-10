#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* RLEcompress(char* str)
{
    char* ret = calloc(2 * strlen(str) + 1, 1);
    size_t retIdx = 0, inIdx = 0;
    while (str[inIdx]) {
        size_t count = 1;
        size_t contIdx = inIdx;
        while (str[inIdx] == str[++contIdx]) {
            count++;
        }
        size_t tmpCount = count;

        // break down counts with 2 or more digits into counts ≤ 9
        while (tmpCount > 9) {
            tmpCount -= 9;
            ret[retIdx++] = str[inIdx];
            ret[retIdx++] = str[inIdx];
            ret[retIdx++] = '9';
        }

        char tmp[2];

        ret[retIdx++] = str[inIdx];
        if (tmpCount > 1) {
            // repeat character (this tells the decompressor that the next digit
            // is in fact the # of consecutive occurrences of this char)
            ret[retIdx++] = str[inIdx];
            // convert single-digit count to string
            snprintf(tmp, 2, "%ld", tmpCount);
            ret[retIdx++] = tmp[0];
        }

        inIdx += count;
    }

    return ret;
}

char* RLEdecompress(char* str, size_t uncompressedSize) {
    char* ret = calloc(uncompressedSize, 1);
    size_t retIdx = 0, inIdx = 0;
    while (str[inIdx]) {
        ret[retIdx++] = str[inIdx];
        if (str[inIdx] == str[inIdx + 1]) { // next digit is the # of occurrences
            size_t occ = ((str[inIdx + 2]) - '0');
            for (size_t i = 1; i < occ; i++) {
                ret[retIdx++] = str[inIdx];
            }
            inIdx += 2;
        }
        inIdx += 1;
    }
    return ret;
}

// int main(int argc, char** argv) {
//     char* s = (RLEcompress(argv[1]));
//     printf("Original: %s\nCompressed: %s\nUncompressed: ", argv[1], s);
//     puts(RLEdecompress(s, strlen(argv[1])));
// }