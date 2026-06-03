// ctype.h の toupper ('a' → 'A' = 65)
// 期待: exit=65
#include <ctype.h>
int main(void) {
    return toupper('a');
}
