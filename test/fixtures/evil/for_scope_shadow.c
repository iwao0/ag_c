// for 宣言子の変数は for スコープ限定
// 期待: exit=99
#include <assert.h>
int main(void) {
    int i = 99;
    for (int i = 0; i < 5; i = i + 1) {}
    assert(i == 99);
    return 0;
}
