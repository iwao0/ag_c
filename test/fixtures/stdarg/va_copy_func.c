// stdarg.h: va_copy で同じ可変引数列を 2 回走査する。
// 修正前: va_copy が未実装で 2 回目の走査が壊れていた。
// 修正後: stdarg.h で va_copy(dest, src) = (dest = src) として定義され、
// __va_arg_area を 2 つの va_list で独立に進められる。
// s = 10+20+30 = 60、t = 10+20+30 = 60、s + t = 120
// 期待: exit=120
#include <stdarg.h>
#include <assert.h>

int twice(int n, ...) {
    va_list ap, copy;
    va_start(ap, n);
    va_copy(copy, ap);
    int s = 0, t = 0;
    int i;
    for (i = 0; i < n; i++) s += va_arg(ap, int);
    for (i = 0; i < n; i++) t += va_arg(copy, int);
    va_end(ap);
    va_end(copy);
    return s + t;
}

int main(void) {
    assert(twice(3, 10, 20, 30) == 120);
    return 0;
}
