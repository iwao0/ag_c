// 深いネスト型の assoc: cast 型と assoc 型が同一なので正しくマッチする (clang と同じ=1)。
// 以前は複雑なキャスト制御式の型照合が未対応で default(2) に落ちていたが、型を正規化トークン
// 文字列にして照合するよう修正し、cast 制御式でもネスト型を区別できるようになった。
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(_Generic((int (*(*)(void))[3])0, int (*(*)(void))[3]: 1, default: 2) == 1);
    return 0;
}
