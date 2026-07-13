// signal.h minimal runtime calls
// Expected: exit=0
#include <signal.h>

static int seen;

static void handler(int sig) {
    if (sig == SIGINT) seen += 7;
}

int main(void) {
    sig_handler_t (*install_handler)(int, sig_handler_t) = signal;
    int (*send_signal)(int) = raise;
    sig_handler_t local_handler = handler;
    if (!install_handler || !send_signal || !local_handler) return 1;
    if (seen != 0) return 2;
    return 0;
}
