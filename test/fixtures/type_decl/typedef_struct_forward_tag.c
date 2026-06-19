// typedef struct S S; (前方宣言) + 後で定義
// 期待: exit=7
#include <assert.h>
typedef struct S S;
struct S { int x; };
int main(void) {
    S s;
    s.x = 7;
    assert(s.x == 7);
    return 0;
}
