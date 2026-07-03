// signal.h minimal runtime calls
// Expected: exit=0
#include <signal.h>

static void handler(int sig) {
    (void)sig;
}

int main(void) {
    sig_handler_t old = signal(SIGINT, handler);
    int rc = raise(SIGINT);
    signal(SIGINT, old);
    return rc != 0;
}
