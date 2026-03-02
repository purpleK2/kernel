#include "types.h"
#ifndef FILE_IO_H
#define FILE_IO_H 1

#include <stddef.h>

typedef enum fseek {
    SEEK_SET = 0, // start of file buffer
    SEEK_CUR = 1, // current offset of file buffer
    SEEK_END = 2  // end of file buffer
} fseek_t;

typedef enum fcntl_cmd {
    F_GETFL = 0,
    F_SETFL = 1,
} fcntl_cmd_t;

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_NOCTTY    0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
#define O_DSYNC     0x1000
#define O_SYNC      0x101000
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW  0x20000
#define O_CLOEXEC   0x80000
#define O_CREATE    O_CREAT

#define PIPE_READ_END  (1 << 20)
#define PIPE_WRITE_END (1 << 21)

#define SPECIAL_FILE_TYPE_PIPE   (1 << 4)
#define SPECIAL_FILE_TYPE_DEVICE (1 << 5)

/*
    Proper structs and functions for file I/O
*/

typedef struct file_io {
    void *buf_start; // actual file data
    size_t size;     // file size

    size_t flags; // flags _/(0 o 0)\_

    // for reading and writing
    size_t offset;

    void *private; // for internal use (aka you put the vnode in here :P)
} fileio_t;

typedef struct vnode vnode_t;
typedef struct dirent dirent_t;

typedef struct dir_handle {
    vnode_t  *vnode;
    dirent_t *entries;
    size_t    count;
    size_t    index;
} dir_handle_t;

fileio_t *fio_create();

fileio_t *open(const char *path, int flags, mode_t mode);
size_t read(fileio_t *file, size_t size, void *out);
int write(fileio_t *file, void *buf, size_t size);
int close(fileio_t *file);

size_t seek(fileio_t *file, size_t offset, fseek_t whence);

size_t fcntl(fileio_t *file, fcntl_cmd_t cmd, void *arg);

int fs_list(const char *path, int max_depth);

#endif
