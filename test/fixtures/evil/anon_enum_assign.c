// 無名 enum + 値指定
// 期待: exit=20
#include <assert.h>
int main(void) {
    enum { A = 10, B = 20, C = 30 };
    assert(B == 20);
    return 0;
}
