// stdbool.h の false マクロ
// 期待: exit=0
#include <stdbool.h>
int main(void) {
    bool b = false;
    return b ? 1 : 0;
}
