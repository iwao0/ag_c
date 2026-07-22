// 整数定数式の型は lvalue conversion 後の int として照合される
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(_Generic(1, int const: 2, default: 3) == 3);
    return 0;
}
