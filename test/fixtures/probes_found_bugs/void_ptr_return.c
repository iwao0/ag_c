// `void *` を返す関数は void 関数ではない (C11 6.7.6.3)。
// stmt.c の return チェックが ret_token_kind==TK_VOID だけを見て is_pointer を
// 無視していたため `void *f(){ return p; }` が E3005 で誤って弾かれていた
// (malloc 風の関数が一切書けない)。is_pointer を加味して修正。
// NULL 返し / オブジェクトアドレス返し / パラメータ経由の各経路を検査する。
#include <assert.h>

int g = 7;
char buf[10];

void *ret_null(void) { return (void *)0; }
void *ret_addr(void) { return &g; }
void *ret_elem(int i) { return &buf[i]; }

int main(void) {
    assert(ret_null() == (void *)0);

    int *p = (int *)ret_addr();
    assert(p == &g);
    assert(*p == 7);

    char *cp = (char *)ret_elem(3);
    *cp = 65;
    assert(buf[3] == 65);
    assert(cp == &buf[3]);
    return 0;
}
