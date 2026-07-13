// if-else 内での return
// 期待: exit=1
#include <assert.h>
int main() {
    int r;
    if (1) r = 1;
    else r = 2;
    assert(r == 1);
    return 0;
}
