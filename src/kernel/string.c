#include <stddef.h>
#include <string.h>

size_t strlen(const char* str) {
    const char* e = str;
    while(*e) e++;
    return e - str;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 || *s2) {
        if (*(++s1) != *(++s2)) return *s1 - *s2;
    }

    return 0;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
    }

    return 0;
}
