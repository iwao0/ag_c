// 通常の文字列リテラルと stringize `#x` の結果を隣接連結できる必要がある
// (`"a" S(b)` → "ab")。stringize 結果トークンは char_width=0 のままで、
// parse_string_literal_sequence の幅比較が 0 を正規化せず CHAR(1) と不一致になり
// E3002 で弾いていた (stringize が先頭の `S(a) "b"` は先頭正規化で通っていた)。
// 比較側でも char_width 0 を CHAR に正規化して修正。
#include <assert.h>

#define S(x) #x

int main(void) {
    const char *p = "a" S(b);      // literal, then stringize
    assert(p[0] == 'a' && p[1] == 'b' && p[2] == 0);

    const char *q = S(c) "d";      // stringize, then literal (元から動作)
    assert(q[0] == 'c' && q[1] == 'd' && q[2] == 0);

    const char *r = "x" S(y) "z";  // literal/stringize/literal の3連結
    assert(r[0] == 'x' && r[1] == 'y' && r[2] == 'z' && r[3] == 0);

    const char *s = "a" "b";       // 通常連結 (退行確認)
    assert(s[0] == 'a' && s[1] == 'b');
    return 0;
}
