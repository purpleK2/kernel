#ifndef STRING_H

#include <stddef.h>

/*
 * Calculate length of a null-terminated string.
 * @param str string to parse
 * @returns length of string (>= 0)
 */
size_t strlen(const char* str);

/*
 * Compares two strings lexicographically, until a null character is reached.
 * @param s1 string
 * @param s2 string
 * @returns 0 if strings are equal, >0 if s1 comes lexicographically after s2, <0 s1 comes lexicographically before s2.
 */
int strcmp(const char* s1, const char* s2);

/*
 * Compares two strings lexicographically, for a determined number of characters.
 * @param s1 string
 * @param s2 string
 * @param n number of characters
 * @returns 0 if strings are equal, >0 if s1 comes lexicographically after s2, <0 s1 comes lexicographically before s2.
 */
int strncmp(const char* s1, const char* s2, size_t n);

#endif
