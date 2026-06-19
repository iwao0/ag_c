// 複合ビットシフト OR
// 1<<1=2, <<2=4, <<3=8, <<4=16 → 30
// 期待: exit=30
#include <assert.h>
int main(void) {
    int x = 1;
    int y = x << 1 | x << 2 | x << 3 | x << 4;
    assert(y == 30);
    return 0;
}
