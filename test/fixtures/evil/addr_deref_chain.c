// *&*&*&x のチェーン
// 期待: exit=42
#include <assert.h>
int main(void) {
    int x = 42;
    int *p = &x;
    assert(*&*&*&x == 42);
    return 0;
}
