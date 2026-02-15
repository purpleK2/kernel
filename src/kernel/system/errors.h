#ifndef ERRORS_H
#define ERRORS_H 1

#define EOK       0
#define ENOMEM    1  // no memory
#define EINVAL    2  // invalid argument
#define EIO       3  // input/output error
#define EACCES    4  // permission denied
#define ENOENT    5  // no such file or directory
#define EBUSY     6  // device or resource busy
#define EUNFB     7  // undefined behavior
#define ENOIMPL   8  // no implementation
#define ENOTDIR   9  // no such directory
#define ENULLPTR  10 // NULL pointer found
#define ENOCFG    11 // invalid/no configuration
#define ENOPID    12 // pid not found
#define EPIPE     13
#define EBADF     14
#define EEXIST    15
#define ELOOP     16 // idk why its named that, this is basically when there are too many symlinks
#define ENOSYS    17 // no function available
#define ENOTEMPTY 18
#define EISDIR    19
#define ENOEXEC   20
#define EACCESS   21 // no permission (haha couldnt imagine)
#define EMFILE    22 // too many open files in system
#define EFAULT    23 // bad address
#define EINTR     24 // interrupted system call
#define ENXIO     25 // no such device or address

#define EPCIENOENT 101 // special
#endif                 // ERRORS_H
