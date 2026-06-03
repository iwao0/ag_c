// グローバル 1D 配列の brace 初期化子。
// 修正前: グローバル変数の `= {...}` が未実装で E3045 (数値が必要) が出ていた。
// 修正後: apply_toplevel_object_initializer で `{` を peek し、init_values[] に
// 行優先で値を flatten して保存。codegen 側で .long で列挙する。
// g[2] = 30
// 期待: exit=30
int g[3] = {10, 20, 30};
int main(void) {
    return g[2];
}
