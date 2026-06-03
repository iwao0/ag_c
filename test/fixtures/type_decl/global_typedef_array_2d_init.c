// グローバル typedef 多次元配列の brace 初期化子。
// `typedef int M2[2][3]; M2 g = {...};` で g は int[2][3] と等価。
// 修正前: typedef 使用側 (`M2 g`) で typedef の dims が捨てられ、g_toplevel
// 状態を経由した sizeof が 4 のみだった。
// 修正後: resolve_toplevel_typedef_ref で ex3 経由で dims を取得し、
// parse_toplevel_array_suffixes が typedef dims を後ろに append する。
// g[1][2] = 60
// 期待: exit=60
typedef int M2[2][3];
M2 g = {{10, 20, 30}, {40, 50, 60}};
int main(void) {
    return g[1][2];
}
