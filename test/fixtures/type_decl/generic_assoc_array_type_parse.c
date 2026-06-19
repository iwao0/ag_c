// 配列型 assoc は ポインタとマッチしない → default
// 期待: exit=0
#include <assert.h>
int main(void) {
    int *p = 0;
    assert(_Generic(p, int[3]: 1, default: 2) == 2);
    return 0;
}
