// signal.h nested function pointer and runtime disposition boundaries
// Expected: exit=0
#include <signal.h>

static volatile sig_atomic_t seen;

static void handler(int sig) {
    if (sig == SIGINT) {
        seen += 7;
    }
}

int main(void) {
    void (*(*install_handler)(int, void (*)(int)))(int) = signal;
    int (*send_signal)(int) = raise;
    void (*previous)(int);
    void (*replaced)(int);

    previous = install_handler(SIGINT, handler);
    if (previous == SIG_ERR) return 1;

    replaced = install_handler(SIGINT, handler);
    if (replaced != handler) {
        install_handler(SIGINT, previous);
        return 2;
    }

    if (send_signal(SIGINT) != 0) {
        install_handler(SIGINT, previous);
        return 3;
    }
    if (seen != 7) {
        install_handler(SIGINT, previous);
        return 4;
    }

    replaced = install_handler(SIGINT, SIG_IGN);
    if (replaced == SIG_ERR) {
        install_handler(SIGINT, previous);
        return 5;
    }
    if (send_signal(SIGINT) != 0) {
        install_handler(SIGINT, previous);
        return 6;
    }
    if (seen != 7) {
        install_handler(SIGINT, previous);
        return 7;
    }

    if (install_handler(SIGINT, previous) != SIG_IGN) return 8;
    return 0;
}
