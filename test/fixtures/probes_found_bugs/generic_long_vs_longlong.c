// C11 6.2.5: long と long long は同サイズ (両方 8B) でも別型。_Generic は型で選択
// するので `_Generic(0LL, long:.., long long:..)` は long long に一致しなければ
// ならない。型照合が size と符号だけで比較していたため long 側に誤マッチしていた。
// 整数リテラルの LL サフィックス (token の int_size) を node/generic_type に伝播し、
// 同サイズ整数では long long の rank も照合して区別する。
#include <assert.h>

int main(void) {
    // リテラルのサフィックスで型が決まる
    assert(_Generic(0,   int:1, long:2, long long:3, default:0) == 1);
    assert(_Generic(0L,  int:1, long:2, long long:3, default:0) == 2);
    assert(_Generic(0LL, int:1, long:2, long long:3, default:0) == 3);

    // unsigned 版
    assert(_Generic(0U,   unsigned int:1, unsigned long:2, unsigned long long:3, default:0) == 1);
    assert(_Generic(0UL,  unsigned int:1, unsigned long:2, unsigned long long:3, default:0) == 2);
    assert(_Generic(0ULL, unsigned int:1, unsigned long:2, unsigned long long:3, default:0) == 3);

    // long 変数は long に一致 (long long 変数/式の rank 追跡は宣言・式型経路の別課題)
    long lv = 0;
    assert(_Generic(lv, long:2, long long:3, default:0) == 2);
    return 0;
}
