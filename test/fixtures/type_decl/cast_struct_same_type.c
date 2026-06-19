// 同型 struct 同士の cast (no-op)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { int x; };
    struct S s = (struct S)(struct S){7};
    assert(s.x == 7);
    return 0;
}
