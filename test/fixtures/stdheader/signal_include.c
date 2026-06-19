// signal.h の SIGINT == 2
// 期待: exit=42
#include <signal.h>
#include <assert.h>
int main(void) {
    assert(SIGINT == 2);
    return 0;
}
