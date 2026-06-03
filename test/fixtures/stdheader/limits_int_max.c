// limits.h の INT_MAX
// 期待: exit=42
#include <limits.h>
int main(void) {
    return INT_MAX == 2147483647 ? 42 : 0;
}
