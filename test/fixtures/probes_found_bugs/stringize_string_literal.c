// プリプロセッサの # (stringize) が文字列リテラル引数の囲み引用符を落とし、内部の
// " と \ もエスケープしていなかった (STR("hi") が "hi" でなく hi になっていた)。
// C11 6.10.3.2 に従い、囲み " を保持し内部の " と \ の前に \ を挿入する。
#include <assert.h>

#define STR(x) #x

int main(void) {
    const char *a = STR("hi");
    assert(a[0] == '"');           /* 先頭は引用符 */
    assert(a[1] == 'h' && a[2] == 'i' && a[3] == '"');
    assert(sizeof(STR("hi")) == 5); /* "\"hi\"" = 4 文字 + NUL */

    const char *b = STR("a\n");    /* 内部のバックスラッシュが保持される */
    assert(b[0] == '"' && b[1] == 'a' && b[2] == '\\' && b[3] == 'n' && b[4] == '"');

    /* 識別子・数値の stringize は不変 */
    const char *c = STR(abc);
    assert(c[0] == 'a' && c[1] == 'b' && c[2] == 'c' && c[3] == 0);
    return 0;
}
