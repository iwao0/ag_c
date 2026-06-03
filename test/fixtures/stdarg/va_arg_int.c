// 可変引数で int を取り出す基本動作 (stdarg.h)
// 期待: exit=42 (10+20+12)
#include <stdarg.h>

int my_sum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    int i;
    for (i = 0; i < n; i++) {
        s += va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

int main(void) {
    return my_sum(3, 10, 20, 12);
}
