#ifndef LIBC_STUBS_H
#define LIBC_STUBS_H

// Replacement for standard C functions not available in xv6
#if 0
long strtol(const char *nptr, char **endptr, int base);
#endif // strol
int isdigit(int c);
int isxdigit(int c);
int islower(int c);
int isupper(int c);
int isspace(int c);
int tolower(int c);
int toupper(int c);

#endif // LIBC_STUBS_H