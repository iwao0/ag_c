// string.h の memset で 0 埋め
// 期待: exit=42
#include <string.h>
int main(void) {
    char buf[4];
    memset(buf, 0, 4);
    return buf[0] == 0 && buf[3] == 0 ? 42 : 0;
}
