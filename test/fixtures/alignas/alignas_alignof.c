// _Alignas(_Alignof(T)) のネスト (C11)
// 修正前: 定数式評価器が _Alignof を認識せず
//        "E2007: 必要な整数がありません" でコンパイル失敗
// 対応: enum_const.c の parse_unary で TK_SIZEOF と TK_ALIGNOF を共通処理
// 期待: exit=42
#include <assert.h>
int main(void) {
    _Alignas(_Alignof(long long)) int x = 42;
    assert(x == 42);
    return 0;
}
