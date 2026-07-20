// 関数の宣言と定義でcanonical関数型を照合する (C11 6.7p4)。同名関数の再宣言で
// 戻り値型、引数数、可変長性、各引数型が異なる場合は E3064 で報告する。
// 本 fixture は「合法な再宣言」 (シグネチャ一致) が
// false-positive で弾かれないことの回帰確認。
//
// 修正前は戻り型のみ照合され、引数数 / 引数型の不一致が見逃されていた:
//   int g(int); int g(int x, int y) { ... }   ← 旧: silently 通過
//   int h(int); int h(double x) { ... }       ← 旧: silently 通過
//
// 現在は `PSX_TYPE_FUNCTION` が戻り値型、固定引数数、可変長性、各canonical引数型を保持し、
// 再宣言時に型木を一括比較する。ABI parameter maskは型identityの比較対象にしない。
#include <assert.h>

/* (a) 同一シグネチャの宣言+定義 */
int add(int, int);
int add(int x, int y) { return x + y; }

/* (b) ポインタ / 多段ポインタ引数 */
int sum_arr(int *a, int n);
int sum_arr(int *a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

/* (c) double 引数 + 戻り値 */
double scale(double v, double f);
double scale(double v, double f) { return v * f; }

/* (d) 可変長 */
int count_args(int n, ...);
int count_args(int n, ...) { return n; }

/* (e) 同一サイズで粒度内 (signed int vs int — 同じ INT4 カテゴリなので合法扱い) */
int with_signed(signed int x);
int with_signed(int x) { return x; }

/* (f) parameter最上位のqualifierは関数型の互換性に影響しない。 */
int pointer_top_const(char *);
int pointer_top_const(char * const text) { return text[0]; }

/* (g) pointerの内側のqualifierは宣言と定義の両方で保持する。 */
int qualified_pointee(const char *);
int qualified_pointee(const char *text) { return text[0]; }

int volatile_pointee(volatile char *);
int volatile_pointee(volatile char *text) { return text[0]; }

int main(void) {
    int arr[] = {1, 2, 3, 4, 5};
    assert(add(3, 4) == 7);
    assert(sum_arr(arr, 5) == 15);
    assert(scale(2.0, 3.5) == 7.0);
    assert(count_args(3, 10, 20, 30) == 3);
    assert(with_signed(42) == 42);
    char text[] = "abc";
    assert(pointer_top_const(text) == 'a');
    assert(qualified_pointee(text) == 'a');
    assert(volatile_pointee(text) == 'a');
    return 0;
}
