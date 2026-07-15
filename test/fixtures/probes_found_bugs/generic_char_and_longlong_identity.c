// _Generic は char/signed char/unsigned char、long/long long を別型として扱う
// (C11 6.2.5/6.7.2.1)。しかし変数の制御式ではサイズ (1 や 8) しか見ておらず、
// plain char が signed char に、long long が long に誤マッチしていた。型識別フラグ
// (is_plain_char / integer_kind) を宣言→lvar→参照ノード→制御式型推論へ伝播して区別する。
#include <assert.h>

int main(void) {
    /* char と signed char と unsigned char は別型 */
    char c = 0;
    assert(_Generic((c), signed char: 2, char: 1, unsigned char: 3, default: 0) == 1);
    signed char sc = 0;
    assert(_Generic((sc), signed char: 2, char: 1, unsigned char: 3, default: 0) == 2);
    unsigned char uc = 0;
    assert(_Generic((uc), signed char: 2, char: 1, unsigned char: 3, default: 0) == 3);

    /* long と long long は別型 (association の順序に依らない) */
    long l = 0;
    assert(_Generic((l), long long: 1, long: 3, default: 0) == 3);
    long long ll = 0;
    assert(_Generic((ll), long long: 1, long: 3, default: 0) == 1);
    unsigned long ul = 0;
    assert(_Generic((ul), unsigned long long: 1, unsigned long: 2, default: 0) == 2);
    unsigned long long ull = 0;
    assert(_Generic((ull), unsigned long long: 1, unsigned long: 2, default: 0) == 1);

    /* 通常のスカラ判定は不変 */
    int i = 0;
    assert(_Generic((i), int: 7, long: 1, default: 0) == 7);
    return 0;
}
