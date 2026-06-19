// -1 >= 0 は偽
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = -1;
    assert(!(x >= 0));
    return 0;
}
