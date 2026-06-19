// for ループに int 宣言と外側変数を使う
// 期待: exit=55 (1..10 の総和)
#include <assert.h>
int main(void) {
    int sum = 0;
    int i;
    for (i = 1; i <= 10; i = i + 1) sum = sum + i;
    assert(sum == 55);
    return 0;
}
