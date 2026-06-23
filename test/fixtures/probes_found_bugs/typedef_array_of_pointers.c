// 続き62: `typedef int *IP[5]` のような「ポインタ配列 typedef」 (修正)。
//
// 修正前: declarator の `*` と `[N]` の両方が typedef 宣言子で組合わさる形が
// is_pointer=1 / is_array=0 / sizeof=8 で「単一ポインタ」と解釈されていた。
// `IP a; a[0] = &g; *a[0]` のように使うと、a は 8 バイトの 1 ポインタ枠しか取れず
// `a[1]` 以降の書き込みが隣接ローカルを破壊して SIGSEGV。
//
// 修正:
// - parser.c::compute_toplevel_typedef_sizeof: ptr_levels>=1 (declarator が `*` を追加) +
//   `[N]` の組合せで sizeof = 8 * N に。
// - parser.c のトップレベル typedef 登録経路で td_is_array を decl_ptr_array にも対応。
// - decl.c::define_local_typedef_from_declarator: 同様の修正で局所 typedef にも対応。
// 結果: typedef は is_array=1 で sizeof_size=N*8 として記録され、既存の「pointer-element
// 1 次元配列 typedef」(`typedef BinOp OpArr3[3]`) と同じ経路で扱われる。
#include <assert.h>

typedef int *IntPtr5[5];
typedef const char *CStrArr3[3];

int g1 = 1, g2 = 2, g3 = 3, g4 = 4, g5 = 5;

int main(void) {
    /* (a) sizeof — N * pointer size (8 バイト) */
    assert(sizeof(IntPtr5) == 5 * 8);
    assert(sizeof(CStrArr3) == 3 * 8);

    /* (b) 局所変数として使う — 全要素にアクセスできる */
    IntPtr5 a;
    a[0] = &g1;
    a[1] = &g2;
    a[2] = &g3;
    a[3] = &g4;
    a[4] = &g5;

    int sum = 0;
    for (int i = 0; i < 5; i++) sum += *a[i];
    assert(sum == 15);

    /* (c) 初期化子で渡す */
    IntPtr5 b = { &g1, &g2, &g3, &g4, &g5 };
    int sum2 = 0;
    for (int i = 0; i < 5; i++) sum2 += *b[i];
    assert(sum2 == 15);

    /* (d) 文字列ポインタ配列 typedef */
    CStrArr3 names = { "alpha", "beta", "gamma" };
    assert(names[0][0] == 'a');
    assert(names[1][0] == 'b');
    assert(names[2][0] == 'g');

    return 0;
}
