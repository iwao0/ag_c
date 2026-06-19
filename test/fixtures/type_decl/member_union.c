// union のメンバ書き込みと読み出し
// 期待: exit=7
#include <assert.h>
int main(void) {
    union U { int x; char y; };
    union U u;
    u.x = 7;
    assert(u.x == 7);
    return 0;
}
