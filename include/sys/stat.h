#ifndef _SYS_STAT_H
#define _SYS_STAT_H

typedef unsigned short mode_t;

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

#ifdef __wasm32__
/*
 * The bundled Wasm runtime owns this compact layout.  Keep it in sync with
 * struct ag_rt_stat in tools/wasm_obj_linker/runtime/parts/stdio.c.
 */
struct stat {
  mode_t st_mode;
  long st_size;
};
#else
/*
 * Apple arm64's fstat ABI writes the 144-byte 64-bit-inode layout even when
 * the declaration comes from this bundled header.  A compact Wasm-shaped
 * definition here corrupts the caller's stack.
 */
#include <time.h>
struct stat {
  int st_dev;
  mode_t st_mode;
  unsigned short st_nlink;
  unsigned long long st_ino;
  unsigned int st_uid;
  unsigned int st_gid;
  int st_rdev;
  struct timespec st_atimespec;
  struct timespec st_mtimespec;
  struct timespec st_ctimespec;
  struct timespec st_birthtimespec;
  long st_size;
  long st_blocks;
  int st_blksize;
  unsigned int st_flags;
  unsigned int st_gen;
  int st_lspare;
  long st_qspare[2];
};
#endif

int fstat(int fd, struct stat *st);

#endif
