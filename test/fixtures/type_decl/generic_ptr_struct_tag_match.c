// _Generic で struct S* マッチ (struct T* と区別)
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int x; };
    struct T { int x; };
    struct S s = {1};
    struct S *ps = &s;
    assert(_Generic(ps, struct T*: 1, struct S*: 2, default: 3) == 2);
    return 0;
}
