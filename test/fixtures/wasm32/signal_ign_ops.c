// wasm32 standalone signal SIG_IGN sentinel behavior
// Expected: exit=0
#include <signal.h>

static int seen;

static void handler(int sig) {
    if (sig == SIGINT) seen += 7;
}

int main(void) {
    if (signal(SIGINT, handler) == SIG_ERR) return 1;
    if (raise(SIGINT) != 0) return 2;
    if (seen != 7) return 3;
    if (signal(SIGINT, SIG_IGN) != handler) return 4;
    if (raise(SIGINT) != 0) return 5;
    if (seen != 7) return 6;
    return 0;
}
