// 可変長関数を関数ポインタ経由で呼ぶと、可変長引数がレジスタ渡しされ Apple ARM64
// の variadic ABI (可変長引数は stack 渡し) に違反し va_arg がゴミを読んでいた。
// 直接呼び出しは正常だった。関数ポインタ lvar に is_variadic_funcptr を記録し、
// 経由呼び出しでも variadic ABI を選ぶよう修正。
#include <stdarg.h>
#include <assert.h>

static int isum(int n, ...) {
    va_list ap; va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}
static double dsum(int n, ...) {
    va_list ap; va_start(ap, n);
    double s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, double);
    va_end(ap);
    return s;
}
static int twofixed(int a, int b, ...) {
    va_list ap; va_start(ap, b);
    int s = a + b;
    for (int i = 0; i < 2; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void) {
    int (*fi)(int, ...) = isum;
    assert(fi(3, 10, 20, 12) == 42);
    assert(fi(1, 100) == 100);

    double (*fd)(int, ...) = dsum;
    assert((int)fd(3, 10.5, 20.5, 11.0) == 42);

    int (*f2)(int, int, ...) = twofixed;
    assert(f2(1, 2, 3, 4) == 10);

    /* 直接呼び出しも維持 */
    assert(isum(2, 5, 5) == 10);
    return 0;
}
