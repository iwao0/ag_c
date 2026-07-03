#ifndef _UNISTD_H
#define _UNISTD_H

long read(int fd, void *buf, unsigned long count);
long write(int fd, const void *buf, unsigned long count);
long lseek(int fd, long offset, int whence);
int close(int fd);

#endif
