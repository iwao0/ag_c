// グローバル 2D 配列の brace 初期化子。
// 修正前: グローバルの多次元配列アクセス自体が outer_stride を持たず、
// `g[i][j]` で stride = elem_size になっていた。
// 修正後: parse_toplevel_array_suffixes で dims を集めて global_var_t に
// outer/mid strides をセット。try_build_global_var_node で ND_ADDR(ND_GVAR) に
// 反映する。
// g[1][2] = 60
// 期待: exit=60
int g[2][3] = {{10, 20, 30}, {40, 50, 60}};
int main(void) {
    return g[1][2];
}
