// stdarg.h: 可変引数として double を取り出す
// Apple ARM64 ABI: variadic 引数は全て caller の stack に置かれる。
// `__va_arg_area` (x29 + STACK_SIZE) を起点に 8B ずつ進めて読む。
// avg(3, 1.0, 2.0, 6.0) = (1.0 + 2.0 + 6.0) / 3 = 3.0
// 期待: exit=3
#include <stdarg.h>

int avg(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double s = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        s += va_arg(ap, double);
    }
    va_end(ap);
    return (int)(s / n);
}

int main(void) {
    return avg(3, 1.0, 2.0, 6.0);
}
