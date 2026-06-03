// ctype.h の isalpha
// 期待: exit=42
#include <ctype.h>
int main(void) {
    return isalpha('A') != 0 ? 42 : 0;
}
