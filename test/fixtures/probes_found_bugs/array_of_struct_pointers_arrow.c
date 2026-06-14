// struct ポインタの配列要素 `arr[i]->m` / `(*arr[i]).m` が E3005 で拒否されるバグ。
// build_subscript_deref が subscript 結果の is_tag_pointer を常に 0 にしていたため、
// 要素が struct ポインタ (`struct N *arr[N]`) のとき「ポインタでない」と誤判定。
// 修正: 要素がポインタ型 (pql>=1) かつ tag を持つとき is_tag_pointer を立てる。
// 修正前: E3005 でコンパイル失敗
// 期待: exit=42
struct N { int v; };
int main(void) {
    struct N a = {10}, b = {20}, c = {12};
    struct N *arr[3] = {&a, &b, &c};
    int s = 0;
    for (int i = 0; i < 3; i++) s += arr[i]->v;   // 10+20+12 = 42
    return (s == 42 && (*arr[1]).v == 20) ? 42 : 0;
}
