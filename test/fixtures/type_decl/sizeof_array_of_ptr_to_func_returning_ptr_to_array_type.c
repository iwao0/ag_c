// 配列 [2] = 16
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(sizeof(int (*(*[2])(void))[3]) == 2 * sizeof(void*));
    return 0;
}
