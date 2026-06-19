// int const * マッチ (= const int *)
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0;
    int const *p = &x;
    assert(_Generic(p, int const *: 2, int *: 1, default: 3) == 2);
    return 0;
}
