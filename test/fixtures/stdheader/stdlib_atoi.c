// stdlib.h の atoi
// 期待: exit=42
#include <stdlib.h>
#include <assert.h>
int main(void) {
    assert(atoi("42") == 42);
    return 0;
}
