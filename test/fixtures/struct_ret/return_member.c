// 戻り値の struct のメンバ参照を上位で行う
// 期待: exit=42 (35+7)
#include <assert.h>
struct Pair { int a; int b; };
struct Pair swap(int a, int b) {
    struct Pair p = {b, a};
    return p;
}
int main(void) {
    struct Pair r = swap(7, 35);
    assert(r.a == 35);
    assert(r.b == 7);
    return 0;
}
