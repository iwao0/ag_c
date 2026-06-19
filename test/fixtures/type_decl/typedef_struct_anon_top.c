// typedef struct { ... } S; (匿名)
// 期待: exit=5
#include <assert.h>
typedef struct { int x; } S;
int main(void) {
    S s;
    s.x = 5;
    assert(s.x == 5);
    return 0;
}
