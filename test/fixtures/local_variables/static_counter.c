// `static int` ローカル変数は関数呼び出しをまたいで値を保持する (C11 6.2.4)
// counter() を 3 回呼ぶと n は 0 → 1 → 2 → 3 と変化、3 回目の戻り値は 3
// 期待: exit=3
#include <assert.h>
int counter(void) {
    static int n = 0;
    return ++n;
}
int main(void) {
    counter();
    counter();
    assert(counter() == 3);
    return 0;
}
