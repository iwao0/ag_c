// _Generic で int 選択
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(_Generic(1, int: 11, default: 22) == 11);
    return 0;
}
