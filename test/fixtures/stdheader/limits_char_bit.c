// limits.h の CHAR_BIT は 8
// 期待: exit=42
#include <limits.h>
int main(void) {
    return CHAR_BIT == 8 ? 42 : 0;
}
