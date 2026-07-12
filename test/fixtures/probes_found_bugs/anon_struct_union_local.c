// タグなし(無名)の struct/union 型をローカル変数として宣言すると、後続文での
// メンバアクセスが E3064 "メンバが存在しません" で失敗するバグ。
// parse_decl_like_stmt が無名タグ名をスタックローカルバッファ char anon_buf[32]
// に格納しており、宣言文の解析後に解放されるため、lvar とタグ登録の tag_name が
// dangling ポインタになり、後の `u.member` でタグ検索が失敗していた。
// 修正: 永続確保する ps_make_anonymous_tag_name を使う (typedef 経路と同じ)。
// 修正前: E3064 でコンパイル失敗
// 期待: exit=42
#include <assert.h>
int main(void) {
    union { int i; struct { short lo, hi; }; } u;
    u.i = 0x00020001;            // lo=1, hi=2 (little-endian)
    struct { int a; int b; } s;
    s.a = 30; s.b = 9;
    assert(u.lo == 1);
    assert(u.hi == 2);
    assert(s.a == 30);
    assert(s.b == 9);
    return 0;
}
