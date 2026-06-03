// assert.h の assert(1) は何もしない
// 期待: exit=42
#include <assert.h>
int main(void) {
    assert(1);
    return 42;
}
