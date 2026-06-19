// _Atomic グローバル + set/get
// 期待: exit=0
#include <assert.h>
_Atomic int ag = 0;
void aset(int v) { ag = v; }
int aget(void) { return ag; }
int main(void) {
    aset(42);
    assert(aget() == 42);
    return 0;
}
