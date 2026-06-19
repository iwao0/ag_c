// _Generic で union 型 assoc
// 期待: exit=0
#include <assert.h>
int main(void) {
    union U { int x; };
    assert(_Generic((union U){.x = 1}, union U: 1, default: 2) == 1);
    return 0;
}
