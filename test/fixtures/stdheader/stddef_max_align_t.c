// stddef.h の max_align_t 型 (C11 7.19)。同梱 stddef.h に未定義だった。
// このターゲットでは clang と同じく max_align_t = long double (size/align = 8)。
// 期待: exit=42
#include <stddef.h>
#include <assert.h>
int main(void) {
    assert(sizeof(max_align_t) == 8);
    assert(_Alignof(max_align_t) == 8);
    return 0;
}
