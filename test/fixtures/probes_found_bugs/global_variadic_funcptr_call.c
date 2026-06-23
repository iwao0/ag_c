// グローバル可変長関数ポインタ経由の呼び出しで、可変長引数がレジスタ渡しされ
// Apple ARM64 variadic ABI (可変長引数は stack 渡し) に違反し va_arg がゴミを読んでいた。
// ローカル funcptr は variadic_via_func_pointer で修正済みだが、トップレベル declarator の
// `(...)` サフィックスが skip_balanced_group だけで `...` を解析せず global_var に
// is_variadic_funcptr が立たなかった。c-testsuite 00189 相当 (fprintf 直呼びなし)。
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

static int fred(int p) {
    printf("yo %d\n", p);
    return 42;
}

static int captured;

static int vprint(FILE *fp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    captured = va_arg(ap, int);
    va_end(ap);
    return fprintf(fp, fmt, captured);
}

int (*f)(int) = &fred;
int (*fprintfptr)(FILE *, const char *, ...) = &vprint;

int main(void) {
    fprintfptr(stdout, "%d\n", (*f)(24));
    assert(captured == 42);
    return 0;
}
