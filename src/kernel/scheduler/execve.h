#ifndef EXECVE_H
#define EXECVE_H

#include <uaccess.h>

int sys_execve(const char __user *path,
               const char __user *const __user *argv,
               const char __user *const __user *envp);

#endif /* EXECVE_H */