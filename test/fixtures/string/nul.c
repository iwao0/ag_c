// エスケープシーケンス '\0' は 0
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert('\0' == 0);
    return 0;
}
