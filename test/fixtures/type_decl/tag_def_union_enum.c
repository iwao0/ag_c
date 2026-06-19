// union と enum の同時定義
// 期待: exit=7
#include <assert.h>
int main(void) {
    union U { int x; char y; };
    enum E { A = 1, B = 2 };
    assert(7 == 7);
    return 0;
}
