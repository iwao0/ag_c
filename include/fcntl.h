#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x0200
#define O_TRUNC  0x0400
#define O_NOFOLLOW 0x0100
#define O_CLOEXEC 0x1000000

int open(const char *path, int oflag, ...);

#endif
