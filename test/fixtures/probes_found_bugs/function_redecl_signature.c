// 関数の宣言と定義でシグネチャを照合する (C11 6.7p4)。同名関数の再宣言で
// (1) 引数数 / 可変長性、(2) 各引数の型カテゴリ (粗粒度: INT4/INT8/FLOAT/DOUBLE/PTR/STRUCT)
// が異なる場合は E3064 で報告する。本 fixture は「合法な再宣言」 (シグネチャ一致) が
// false-positive で弾かれないことの回帰確認。
//
// 修正前は戻り型のみ照合され、引数数 / 引数型の不一致が見逃されていた:
//   int g(int); int g(int x, int y) { ... }   ← 旧: silently 通過
//   int h(int); int h(double x) { ... }       ← 旧: silently 通過
//
// 修正:
// (1) psx_ctx_track_function_nargs を追加し、初回登録/以降比較で引数数 + 可変長性を照合。
// (2) psx_ctx_track_function_param_category を追加し、各引数を粗粒度カテゴリで分類。
//     funcdef の param 走査内で track し、不一致なら E3064。
//     粗粒度なので `int` 系 (char/short/int) や `long` 系 (long/long long) は区別しない (K&R 互換)。
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

int main(void) {
    int arr[] = {1, 2, 3, 4, 5};
    assert(add(3, 4) == 7);
    assert(sum_arr(arr, 5) == 15);
    assert(scale(2.0, 3.5) == 7.0);
    assert(count_args(3, 10, 20, 30) == 3);
    assert(with_signed(42) == 42);
    return 0;
}
