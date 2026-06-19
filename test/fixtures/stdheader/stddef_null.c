// stddef.h の NULL マクロ
// 期待: exit=42
#include <stddef.h>
#include <assert.h>
int main(void) {
    void *p = NULL;
    assert(p == NULL);
    return 0;
}
