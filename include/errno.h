#ifndef _ERRNO_H
#define _ERRNO_H

int *__error(void);
#define errno (*__error())

#define EPERM  1
#define ENOENT 2
#define EIO    5
#define EBADF  9
#define EACCES 13
#define EEXIST 17
#define ENOTDIR 20
#define EDOM   33
#define ERANGE 34
#define ENAMETOOLONG 36
#define EINVAL 22
#define EFBIG  27
#define ENOMEM 12
#define ELOOP  62
#define ENOSYS 78
#define EILSEQ 92

#endif
