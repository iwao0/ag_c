// 最上位ビット(bit63)が立つ整数リテラル (値 > LLONG_MAX) の値欠落。
// token_signed_from_u64() が `uval & LLONG_MAX` で bit63 をマスクして捨てるため、
// 0xFFFFFFFFFFFFFFFF が 0x7FFFFFFFFFFFFFFF として格納されていた。
// 修正前: (0xFFFF...F >> 60) が 7 になる (正しくは 15)。
// 期待: exit=15
#include <assert.h>
int main(void) {
    unsigned long u = 0xFFFFFFFFFFFFFFFFUL;
    assert((int)(u >> 60) == 15); return 0;
}
