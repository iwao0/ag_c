// string.h の memset で 0 埋め
// 期待: exit=42
#include <string.h>
#include <assert.h>
int main(void) {
    char buf[4];
    memset(buf, 0, 4);
    assert(buf[0] == 0);
    assert(buf[3] == 0);
    return 0;
}
