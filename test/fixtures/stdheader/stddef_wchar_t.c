// stddef.h の wchar_t 型 (C11 7.19)。同梱 stddef.h に未定義で E3066 になっていた。
// このターゲットでは clang と同じく wchar_t = int (4B)。
// 期待: exit=42
#include <stddef.h>
#include <assert.h>
int main(void) {
    wchar_t c = L'*';   // 42
    assert(sizeof(wchar_t) == 4);
    assert((int)c == 42);
    return 0;
}
