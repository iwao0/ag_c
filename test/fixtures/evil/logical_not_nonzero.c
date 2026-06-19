// !5 = 0
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 5;
    assert(!x == 0);
    return 0;
}
