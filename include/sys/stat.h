#ifndef _SYS_STAT_H
#define _SYS_STAT_H

typedef unsigned short mode_t;

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

struct stat {
  mode_t st_mode;
  long st_size;
};

int fstat(int fd, struct stat *st);

#endif
