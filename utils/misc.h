#ifndef MISC_H
#define MISC_H

#include <errno.h>
#include <math.h>
#include <ctype.h>

/**
 * \brief Controlla se la stringa passata come primo argomento e' un numero.
 * \return  0 ok  1 non e' un numbero   2 overflow/underflow
 */
static inline int isNumber(const char* s, long* n) {
    if (s == NULL) return 1;
    if (strlen(s) == 0) return 1;
    char* e = NULL;
    errno = 0;
    long val = strtol(s, &e, 10);
    if (errno == ERANGE) return 2;    // overflow/underflow
    if (e != NULL && *e == (char)0) {
        *n = val;
        return 0;   // successo 
    }
    return 1;   // non e' un numero
}

char* numToStr(long n) {
    /**
     * @brief Takes in a `long`; returns that number as a string.
     *
     * @param n The number fo stringify.
     * @note This function allocates memory on the heap. The caller needs to call `free` on it.
     *
     * @return The given number a string.
     */
    bool negative = (n < 0);
    const size_t nDigits = n ? (floor(log10(abs(n))) + 1) : 1; // a base-10 number always has 1 + int-part of its log10 digits
    char* ret = calloc(nDigits + (negative ? 2 : 1), 1);
    if (!ret) {
        errno = ENOMEM;
        return NULL;
    }
    for (size_t i = 0; i < nDigits; i++) {
        unsigned short currDigit = abs(n) % 10;
        ret[nDigits - (negative ? 0 : 1) - i] = '0' + currDigit;
        n /= 10;
    }
    if (negative) {
        ret[0] = '-';
    }
    return ret;
}

#endif