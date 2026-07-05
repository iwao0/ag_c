// signal.h の SIGINT == 2
// 期待: exit=42
#include <signal.h>
#include <assert.h>
int main(void) {
    assert(SIGINT == 2);
    assert(SIG_DFL == (sig_handler_t)0);
    assert(SIG_IGN == (sig_handler_t)1);
    assert(SIG_ERR != SIG_DFL);
    assert(SIG_IGN != SIG_DFL);
    assert(SIG_IGN != SIG_ERR);
    return 0;
}
