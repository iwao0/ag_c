// ポインタ to 関数 returning ポインタ to 配列 = 8
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(sizeof(int (*(*)(void))[3]) == sizeof(void*));
    return 0;
}
