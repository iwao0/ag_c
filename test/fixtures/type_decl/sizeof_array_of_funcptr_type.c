// 関数ポインタ配列 [3] の sizeof = 24
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(sizeof(int (*[3])(int)) == 3 * sizeof(void*));
    return 0;
}
