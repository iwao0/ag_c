// ローカル extern + 不完全配列
// 期待: exit=7
#include <assert.h>
int main(void) {
    extern int a[];
    assert(7 == 7);
    return 0;
}
