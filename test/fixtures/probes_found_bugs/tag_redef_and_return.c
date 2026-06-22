// タグ再定義検出と非 void 関数の戻り値なし警告。
//
// 修正:
// (a) `struct S { int x; }; struct S { int y; };` のタグ再定義が silently 通過し、
//     後段でメンバが混在 / shadow されていた。psx_ctx_define_tag_type_with_layout で
//     同一スコープに既存の完全型 (member_count > 0) があり、今回も完全型 (member_count > 0)
//     なら C11 6.7.2.1p1 違反として E3005 を発する。前方宣言 → 定義は従来どおり update。
//     enum タグも同じ機構で検出。
// (b) `int get(int x) { x = x + 1; }` のように非 void 関数で値を返さずに終端するのが
//     silently 通過していた (C11 6.9.1p12 未定義動作)。emit_implicit_return_if_missing で
//     main 以外の非 void 関数では W3001 warning を発する。main は C11 5.1.2.2.3 で
//     暗黙 return 0 が標準化されているので例外。
//
// 副次: 同一プロセス内で複数回 ps_program_from を呼ぶユニットテストで前回パースの
// "タグ完全定義済み" / "関数 is_defined" 状態が漏れないよう、psx_ctx_reset_tag_diag_state
// と psx_ctx_reset_function_diag_state を ps_program_from の冒頭で呼び出すように。
// 実コンパイル (1 ファイル 1 プロセス) には影響しない。
//
// 本 fixture は合法形 (forward decl + def、void 関数で return なし、main で return なし)
// の回帰確認。
#include <assert.h>

/* (a) 前方宣言 → 完全定義 (合法) */
struct Forward;
struct Forward { int v; };

/* (b) 関数のプロトタイプと定義 */
int get_v(struct Forward *p) {
    return p->v;
}

/* (c) void 関数で return 文なし (合法) */
void do_nothing(int x) {
    (void)x;
    /* return なし: void なので合法 */
}

/* (d) 値を返す関数で末尾 return あり (合法) */
int triple(int x) {
    return x * 3;
}

int main(void) {
    struct Forward f = { .v = 42 };
    assert(get_v(&f) == 42);
    do_nothing(7);
    assert(triple(5) == 15);
    /* main は return 文なしでも C11 5.1.2.2.3 で暗黙 return 0 が標準化されている。
     * 本 fixture は明示的に return 0 を書く。 */
    return 0;
}
