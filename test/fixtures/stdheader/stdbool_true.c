// stdbool.h の bool 型と true マクロ
// 期待: exit=42
#include <stdbool.h>
int main(void) {
    bool b = true;
    return b ? 42 : 0;
}
