#include <dirent.h>
#include <stdio.h>

int main(int argc, char **argv) {
    printf("Hello, World!\n");

    DIR *dir = opendir("/dev/input");
    if (!dir) {
        perror("opendir");
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
    }
    return 0;
}