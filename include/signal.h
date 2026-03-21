#ifndef _SIGNAL_H
#define _SIGNAL_H

#define SIGABRT  6
#define SIGFPE   8
#define SIGILL   4
#define SIGINT   2
#define SIGSEGV 11
#define SIGTERM 15

typedef void (*sig_handler_t)(int);

sig_handler_t signal(int sig, sig_handler_t handler);
int raise(int sig);

#endif
