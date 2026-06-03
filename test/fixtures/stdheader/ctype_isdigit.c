// ctype.h の isdigit
// 期待: exit=42
#include <ctype.h>
int main(void) {
    return isdigit('5') != 0 ? 42 : 0;
}
