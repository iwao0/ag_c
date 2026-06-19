// _Generic で int* 選択
// 期待: exit=0
#include <assert.h>
int main(void) {
    int *p = 0;
    assert(_Generic(p, int*: 3, default: 7) == 3);
    return 0;
}
