// 変数間コピー
// 期待: exit=1
#include <assert.h>
int main(void) {
    int a = 1;
    int b = a;
    assert(b == 1);
    return 0;
}
