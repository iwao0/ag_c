// for 初期化部で複数宣言
// i=0,1,2 の合計 3
// 期待: exit=3
#include <assert.h>
int main(void) {
    int s = 0;
    for (int i = 0, j = 3; i < j; i = i + 1) s = s + i;
    assert(s == 3);
    return 0;
}
