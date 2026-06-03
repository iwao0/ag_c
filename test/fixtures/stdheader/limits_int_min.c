// limits.h の INT_MIN は負数
// 期待: exit=42
#include <limits.h>
int main(void) {
    return INT_MIN < 0 ? 42 : 0;
}
