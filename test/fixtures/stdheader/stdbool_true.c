// stdbool.h の bool 型と true マクロ
// 期待: exit=42
#include <stdbool.h>
#include <assert.h>
int main(void) {
    bool b = true;
    assert(b ? 1 : 0);
    return 0;
}
