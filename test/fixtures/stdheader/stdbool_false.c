// stdbool.h の false マクロ
// 期待: exit=0
#include <stdbool.h>
#include <assert.h>
int main(void) {
    bool b = false;
    assert(!(b ? 1 : 0));
    return 0;
}
