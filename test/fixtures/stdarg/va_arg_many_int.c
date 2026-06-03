// stdarg.h: 多数の int 引数 (16 個) を渡す。
// Apple ARM64 ABI では variadic 引数は全て stack に積まれるので、
// レジスタ数 (x0..x7) の上限とは無関係に多数の引数が渡せることを確認する。
// sum_many(16, 1, 2, ..., 16) = 1+2+...+16 = 136
// 期待: exit=136
#include <stdarg.h>

int sum_many(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    int i;
    for (i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void) {
    return sum_many(16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
}
