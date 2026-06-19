// エスケープシーケンス '\n' は 10 (改行)
// 期待: exit=10
#include <assert.h>
int main(void) {
    assert('\n' == 10);
    return 0;
}
