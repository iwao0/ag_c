// va_copy(b, a) で a の現在状態を b にコピーし、同じ可変引数列を 2 回走査する
// 修正前: stdarg.h に va_copy マクロが無く「未初期化変数 'b' が使用されている」
//        警告 + コード生成異常でリンク失敗
// 対応: include/stdarg.h に `#define va_copy(dest, src) ((dest) = (src))` 追加
// 期待: exit=12 ((1+2+3) + (1+2+3))
#include <stdarg.h>
int sum_twice(int n, ...) {
    va_list a, b;
    va_start(a, n);
    va_copy(b, a);
    int s1 = 0, s2 = 0;
    int i;
    for (i = 0; i < n; i++) s1 += va_arg(a, int);
    for (i = 0; i < n; i++) s2 += va_arg(b, int);
    va_end(a);
    va_end(b);
    return s1 + s2;
}
int main(void) {
    return sum_twice(3, 1, 2, 3);
}
