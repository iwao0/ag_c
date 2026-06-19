// union の指定初期化子 (.x=7)
// 期待: exit=7
#include <assert.h>
int main(void) {
    union U { int x; char y; };
    union U u = {.x = 7};
    assert(u.x == 7);
    return 0;
}
