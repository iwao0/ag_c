// stddef.h の NULL マクロ
// 期待: exit=42
#include <stddef.h>
int main(void) {
    void *p = NULL;
    return p == NULL ? 42 : 0;
}
