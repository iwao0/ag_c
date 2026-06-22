// 識別子の名前空間 (C11 6.2.3) 衝突の検出と、extern と定義間の型整合性。
// 修正前は以下が silently 通過していた:
//   - `extern int g; double g = 1.5;` (extern と定義の型不一致)
//   - `int foo(int){...} int foo;` (関数名 → 変数名)
//   - `int bar; int bar(int){...}` (変数名 → 関数名)
//   - `typedef int T; int T = 5;` (typedef 名 → 変数名)
// それぞれ E3064 で診断するよう、register_toplevel_global_decl で psx_ctx_has_function_name /
// psx_ctx_find_typedef_name を、funcdef で find_global_var_by_name を check するように。
// extern + 定義の型互換チェックも register_toplevel_global_decl の merge 経路に統合。
//
// 本 fixture は合法形 (同型 extern + 定義、関数だけ宣言、typedef を変数の型として使用) の
// 回帰確認。
#include <assert.h>

/* (a) extern + 同型定義 (合法) */
extern int g_int;
int g_int = 42;

extern int g_arr[];          /* extern 不完全配列 */
int g_arr[3] = {1, 2, 3};

/* (b) struct を返す関数 + 関連 typedef + 別変数 (名前は別) */
typedef int Score;
int score_var = 100;          /* Score とは別名 */

/* (c) 関数だけ宣言 (本体なし) + 別の同名でない変数 */
int helper(int);
int helper_var = 7;
int helper(int x) { return x + 1; }

int main(void) {
    assert(g_int == 42);
    assert(g_arr[0] == 1 && g_arr[2] == 3);
    assert(score_var == 100);
    Score s = 5;
    assert(s == 5);
    assert(helper(10) == 11);
    assert(helper_var == 7);
    return 0;
}
