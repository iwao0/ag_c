#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>

#ifndef _BLKSIZE_T
#define _BLKSIZE_T
typedef int blksize_t;
#endif

#ifndef _DEV_T
#define _DEV_T
typedef int dev_t;
#endif

#ifndef _GID_T
#define _GID_T
typedef unsigned int gid_t;
#endif

#ifndef _INO_T
#define _INO_T
typedef unsigned long long ino_t;
#endif

#ifndef _MODE_T
#define _MODE_T
typedef unsigned short mode_t;
#endif

#ifndef _NLINK_T
#define _NLINK_T
typedef unsigned short nlink_t;
#endif

#ifndef _OFF_T
#define _OFF_T
#ifdef __wasm32__
typedef long off_t;
#else
typedef long long off_t;
#endif
#endif

#ifndef _BLKCNT_T
#define _BLKCNT_T
#ifdef __wasm32__
typedef long blkcnt_t;
#else
typedef long long blkcnt_t;
#endif
#endif

#ifndef _PID_T
#define _PID_T
typedef int pid_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
typedef long ssize_t;
#endif

#ifndef _UID_T
#define _UID_T
typedef unsigned int uid_t;
#endif

#endif
