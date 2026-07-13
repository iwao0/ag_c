// struct/union ポインタを返す関数の結果に `->` を使うと E3005 で拒否されるバグ。
// (1) ND_FUNCALL にポインタ戻り型が伝播せず、値型と誤判定していた。
// (2) 関数定義のタグ登録が値返し (!ret_is_ptr) のときだけで、ポインタ返しの
//     タグが記録されていなかった。
// 現在は関数の canonical return type を ND_FUNCALL に直接保持する。
// 修正前: E3005 でコンパイル失敗
// 期待: exit=42
#include <assert.h>
struct N { int v; };
struct N g = {30};
struct N *get(void) { return &g; }          // ポインタ返し (グローバル)
struct N *idp(struct N *p) { return p; }     // ポインタ返し (引数)
int main(void) {
    struct N x = {12};
    assert(get()->v == 30); assert(idp(&x)->v == 12); return 0;            // 30 + 12 = 42
}
