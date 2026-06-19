// 整数定数式は const int としてマッチ (ag_c 実装)
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(_Generic(1, int const: 2, default: 3) == 2);
    return 0;
}
