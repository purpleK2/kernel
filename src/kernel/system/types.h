#ifndef TYPES_H
#define TYPES_H

#include <stdatomic.h>
#include <stdint.h>

// user management

typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int id_t;
typedef unsigned int mode_t;

// file management

typedef unsigned int fd_t;

typedef uint64_t pid_t;

typedef int64_t off_t;

#endif // TYPES_H