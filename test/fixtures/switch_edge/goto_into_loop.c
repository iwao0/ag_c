// goto でループの内側へジャンプ
// goto enter → x+=2、x<100 の間 x+=1, +=2 を繰り返す → 101 で停止
// 期待: exit=101
#include <assert.h>
int main(void) {
    int x = 0;
    goto enter;
    while (x < 100) {
        x = x + 1;
enter:
        x = x + 2;
    }
    assert(x == 101);
    return 0;
}
