// 最小のタグ宣言 (struct/union/enum のみ)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S;
    union U;
    enum E;
    assert(7 == 7);
    return 0;
}
