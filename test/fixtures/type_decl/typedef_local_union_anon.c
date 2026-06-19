// ローカル typedef + 匿名 union
// 期待: exit=4
#include <assert.h>
int main(void) {
    typedef union { int y; } L;
    L l;
    l.y = 4;
    assert(l.y == 4);
    return 0;
}
