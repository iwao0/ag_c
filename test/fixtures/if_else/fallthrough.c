// else が無い場合、if 側で return すれば後続には来ない
// 期待: exit=42
#include <assert.h>
main() {
    int r = 0;
    if (1) r = 42;
    assert(r == 42);
    return 0;
}
