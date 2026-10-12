#include <string.h>

size_t strlen(const char* str) {
    const char* e = str;
    while(*e) e++;
    return e - str;
}
