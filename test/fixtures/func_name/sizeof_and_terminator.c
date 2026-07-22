// __func__ は関数名で初期化された配列として扱われるため、sizeof は
// 終端 NUL を含み、配列の末尾も読み出せる (C11 6.4.2.2)。
#include <assert.h>

static int named(void) {
    assert(sizeof(__func__) == sizeof("named"));
    assert(__func__[5] == '\0');
    return __func__[0];
}

int main(void) {
    assert(named() == 'n');
    return 0;
}
