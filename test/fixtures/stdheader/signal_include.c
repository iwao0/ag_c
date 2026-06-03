// signal.h の SIGINT == 2
// 期待: exit=42
#include <signal.h>
int main(void) {
    return SIGINT == 2 ? 42 : 0;
}
