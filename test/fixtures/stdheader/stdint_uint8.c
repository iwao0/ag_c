// stdint.h の uint8_t 型: unsigned char typedef なので (int) キャストは
// ゼロ拡張され 200 を保つ (符号拡張で -56 にならないこと)。
#include <stdint.h>
#include <assert.h>
int main(void) {
    uint8_t x = 200;
    assert((int)x == 200);
    return 0;
}
