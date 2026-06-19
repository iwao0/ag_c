// while ループで _Thread_local を更新
// 期待: exit=0
#include <assert.h>
_Thread_local int tc = 0;
int main(void) {
    int i = 0;
    while (i < 10) { tc = tc + 1; i = i + 1; }
    assert(tc == 10);
    return 0;
}
