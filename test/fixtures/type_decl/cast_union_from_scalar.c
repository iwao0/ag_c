// スカラから union への cast (拡張)
// 期待: exit=7
#include <assert.h>
int main(void) {
    union U { int x; char y; };
    assert(((union U)7).x == 7);
    return 0;
}
