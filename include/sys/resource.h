#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)

#ifdef __wasm32__
/*
 * The sandboxed Wasm runtimes do not expose host resource accounting.
 * Keep the minimal shape used by their explicit ENOSYS implementation.
 */
struct rusage {
  long ru_maxrss;
};
#else
/*
 * Apple arm64's getrusage ABI writes two timevals followed by fourteen
 * longs.  Using the compact Wasm shape here overwrites the caller's stack.
 */
#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL struct timeval
struct timeval {
  long tv_sec;
  int tv_usec;
};
#endif

struct rusage {
  struct timeval ru_utime;
  struct timeval ru_stime;
  long ru_maxrss;
  long ru_ixrss;
  long ru_idrss;
  long ru_isrss;
  long ru_minflt;
  long ru_majflt;
  long ru_nswap;
  long ru_inblock;
  long ru_oublock;
  long ru_msgsnd;
  long ru_msgrcv;
  long ru_nsignals;
  long ru_nvcsw;
  long ru_nivcsw;
};
#endif

int getrusage(int who, struct rusage *usage);

#endif
