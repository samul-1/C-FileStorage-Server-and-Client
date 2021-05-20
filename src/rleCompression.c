#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

char* RLEcompress(char* data, size_t origSize, size_t* compressedSize) {
    char* ret = calloc(2 * origSize, 1);
    size_t retIdx = 0, inIdx = 0;
    size_t retSize = 0;
    while (inIdx < origSize) {
        size_t count = 1;
        size_t contIdx = inIdx;
        while (contIdx < origSize - 1 && data[inIdx] == data[++contIdx]) {
            count++;
        }
        size_t tmpCount = count;

        // break down counts with 2 or more digits into counts ≤ 9
        while (tmpCount > 9) {
            tmpCount -= 9;
            ret[retIdx++] = data[inIdx];
            ret[retIdx++] = data[inIdx];
            ret[retIdx++] = '9';
            retSize += 3;
        }

        ret[retIdx++] = data[inIdx];
        retSize += 1;
        if (tmpCount > 1) {
            // repeat character (this tells the decompressor that the next digit
            // is in fact the # of consecutive occurrences of this char)
            ret[retIdx++] = data[inIdx];
            // convert single-digit count to dataing
            ret[retIdx++] = '0' + tmpCount;
            retSize += 2;
        }

        inIdx += count;
    }
    *compressedSize = retSize;
    return ret;
}

char* RLEdecompress(char* data, size_t compressedSize, size_t uncompressedSize, size_t extraAllocation) {
    char* ret = calloc(uncompressedSize + extraAllocation, 1);
    size_t retIdx = 0, inIdx = 0;
    while (inIdx < compressedSize) {
        ret[retIdx++] = data[inIdx];
        if (data[inIdx] == data[inIdx + 1]) { // next digit is the # of occurrences
            size_t occ = ((data[inIdx + 2]) - '0');
            for (size_t i = 1; i < occ && retIdx < compressedSize; i++) {
                ret[retIdx++] = data[inIdx];
            }
            inIdx += 2;
        }
        inIdx += 1;
    }
    return ret;
}
