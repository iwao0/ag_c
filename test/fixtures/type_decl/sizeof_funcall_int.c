// sizeof(funcall()) は戻り値の型サイズを返す (副作用は評価しない)
// int 戻り値関数 → 4
// 期待: exit=4
#include <assert.h>
int identity(int x) { return x; }
int main(void) {
    assert((int)sizeof(identity(42)) == 4);
    return 0;
}
