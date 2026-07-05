// signal.h minimal runtime calls
// Expected: exit=0
#include <signal.h>

static int seen;

static void handler(int sig) {
    if (sig == SIGINT) seen += 7;
}

int main(void) {
    sig_handler_t old = signal(SIGINT, handler);
    sig_handler_t prev = signal(SIGINT, handler);
    if (prev != handler) return 1;
    int rc = raise(SIGINT);
    if (rc != 0) return 2;
    if (seen != 7) return 3;
    if (raise(-1) == 0) return 4;
    if (signal(-1, handler) != SIG_ERR) return 5;
    if (signal(SIGINT, SIG_IGN) != handler) return 6;
    if (raise(SIGINT) != 0) return 7;
    if (seen != 7) return 8;
    signal(SIGINT, old);
    return 0;
}
