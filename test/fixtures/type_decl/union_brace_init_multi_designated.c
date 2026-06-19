// union に複数指定 (最後の y=2 が有効、他は上書き)
// 期待: exit=2
#include <assert.h>
int main(void) {
    union U { int x; char y; };
    union U u = {.x = 7, .y = 2};
    assert(u.y == 2);
    return 0;
}
