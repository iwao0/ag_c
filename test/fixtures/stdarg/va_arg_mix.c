// stdarg.h: int と double を交互に取り出す
// 固定引数 (n=3) は x0、可変引数は stack に並ぶ:
//   [+0]=10 (int)、[+8]=2.5 (double)、[+16]=7 (int)
// 結果: 10 + (int)2.5 + 7 = 19
// 期待: exit=19
#include <stdarg.h>
#include <assert.h>

int mix(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (i % 2 == 0) {
            s += va_arg(ap, int);
        } else {
            s += (int)va_arg(ap, double);
        }
    }
    va_end(ap);
    return s;
}

int main(void) {
    assert(mix(3, 10, 2.5, 7) == 19);
    return 0;
}
