// 関数を跨いだ _Thread_local 更新
// 期待: exit=0
#include <assert.h>
_Thread_local int tg = 0;
void tinc(void) { tg = tg + 1; }
int main(void) {
    tinc(); tinc(); tinc();
    assert(tg == 3);
    return 0;
}
