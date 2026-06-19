// for ループの終了条件 (10 回回って a=10)
// 期待: exit=10
#include <assert.h>
int main(void) {
    int a;
    for (a = 0; a < 10; a = a + 1) a;
    assert(a == 10);
    return 0;
}
