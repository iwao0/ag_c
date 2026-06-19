// const 修飾仮引数
// 期待: exit=0
#include <assert.h>
int id(const int x) { return x; }
int main(void) {
    assert(id(7) == 7);
    return 0;
}
