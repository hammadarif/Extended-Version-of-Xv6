#include "libc_stub.h"

#if 0
long
strtol(const char *nptr, char **endptr, int base)
{
    long result = 0;
    int sign = 1;

    /* 1) Skip leading whitespace */
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' ||
           *nptr == '\r' || *nptr == '\f' || *nptr == '\v') {
        nptr++;
    }

    /* 2) Check for optional sign */
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    /* If base is zero or 10, assume decimal by default. 
       For a minimal approach, we just default to decimal. */
    if (base == 0) {
        base = 10;
    }

    /* 3) Parse digits until we hit a non-digit or the end of string */
    while (*nptr) {
        int digit;

        /* Convert character to its digit value */
        if (*nptr >= '0' && *nptr <= '9') {
            digit = *nptr - '0';
        } else {
            /* Non-digit character, stop. */
            break;
        }

        /* If digit is invalid for the given base, stop. */
        if (digit >= base) {
            break;
        }

        /* Accumulate into result */
        result = result * base + digit;
        nptr++;
    }

    /* 4) If endptr is not NULL, set it to the point where we stopped */
    if (endptr) {
        *endptr = (char *)nptr;
    }

    return sign * result;
}
#endif

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int isxdigit(int c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int islower(int c) {
    return c >= 'a' && c <= 'z';
}

int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int tolower(int c) {
    return isupper(c) ? c + ('a' - 'A') : c;
}

int toupper(int c) {
    return islower(c) ? c - ('a' - 'a') : c;
}