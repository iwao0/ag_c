#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#define RUSAGE_SELF 0

struct rusage {
  long ru_maxrss;
};

int getrusage(int who, struct rusage *usage);

#endif
