// stdint.h の uint8_t 型
// 期待: exit=200
#include <stdint.h>
int main(void) {
    uint8_t x = 200;
    return (int)x;
}
