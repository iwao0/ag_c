// 識別子周りの診断追加に対する false-positive 確認用 fixture。
//
// 修正:
// (a) 関数識別子への代入 (`f = 5;`): IR builder 段の "ir build/emit failed" 粗エラーから、
//     parser 段の明確な E3064 "関数識別子に代入することはできません" に改善。
// (b) enum 定数と通常 identifier の名前空間衝突 (`enum E{A=5}; int A=10;` や逆順):
//     register_toplevel_global_decl で psx_ctx_find_enum_const を check、enum 定義側でも
//     psx_find_global_var / psx_ctx_has_function_name / psx_ctx_find_typedef_name を check。
// (c) 未宣言関数の呼び出し (`undecl_func()`): C99/C11 で implicit function declaration は不可。
//     build_unqualified_call で psx_ctx_has_function_name と psx_find_global_var の両方に
//     見つからない場合に W3001 warning。
//
// 本 fixture は合法形 (関数の通常呼び出し、enum 定数の参照、関数アドレス取得) で
// false-positive がないことを確認する。
#include <assert.h>

/* (a) 関数の宣言と通常呼び出し / アドレス取得 (legal) */
int add(int, int);
int add(int x, int y) { return x + y; }

/* (b) enum 定数 — 通常 identifier として参照 */
enum Color { RED = 1, GREEN, BLUE };

/* (c) enum 定数を含む式 / switch */
int classify(enum Color c) {
    switch (c) {
        case RED:   return 100;
        case GREEN: return 200;
        case BLUE:  return 300;
    }
    return 0;
}

int main(void) {
    assert(add(3, 4) == 7);

    /* 関数のアドレス取得 (`f`/`&f` どちらも合法) */
    int (*fp)(int, int) = add;
    assert(fp(10, 20) == 30);
    int (*fp2)(int, int) = &add;
    assert(fp2(5, 6) == 11);

    assert(RED == 1 && GREEN == 2 && BLUE == 3);
    assert(classify(RED) == 100);
    assert(classify(BLUE) == 300);

    return 0;
}
