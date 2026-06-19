// 配列型 assoc (関数ポインタ配列) はマッチしない → default
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(_Generic((int (*)(int))0, int (*[3])(int): 1, default: 2) == 2);
    return 0;
}
