// ローカル typedef + 匿名 struct
// 期待: exit=9
#include <assert.h>
int main(void) {
    typedef struct { int y; } L;
    L l;
    l.y = 9;
    assert(l.y == 9);
    return 0;
}
