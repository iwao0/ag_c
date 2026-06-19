// 2D 配列へのポインタの sizeof(*p)
// 期待: exit=48 (3*4*4)
#include <assert.h>
int main(void) {
    int (*p)[3][4];
    assert(sizeof(*p) == 48);
    return 0;
}
