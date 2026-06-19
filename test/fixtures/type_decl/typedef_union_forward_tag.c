// typedef union U U; (前方宣言)
// 期待: exit=8
#include <assert.h>
typedef union U U;
union U { int x; };
int main(void) {
    U u;
    u.x = 8;
    assert(u.x == 8);
    return 0;
}
