// struct/union ポインタを返す関数の結果に `->` を使うと E3005 で拒否されるバグ。
// (1) psx_node_get_tag_type の ND_FUNCALL 分岐が is_tag_pointer を常に 0 にして
//     いたため、ポインタ返しでも「ポインタでない」と誤判定していた。
// (2) 関数定義のタグ登録が値返し (!ret_is_ptr) のときだけで、ポインタ返しの
//     タグが記録されていなかった。
// 修正: ポインタ返しでもタグを登録し、is_tag_pointer は
//       psx_ctx_get_function_ret_is_pointer から取得する。
// 修正前: E3005 でコンパイル失敗
// 期待: exit=42
struct N { int v; };
struct N g = {30};
struct N *get(void) { return &g; }          // ポインタ返し (グローバル)
struct N *idp(struct N *p) { return p; }     // ポインタ返し (引数)
int main(void) {
    struct N x = {12};
    return get()->v + idp(&x)->v;            // 30 + 12 = 42
}
