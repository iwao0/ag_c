// 条件演算子が `&x` を返すとき結果が 4 バイトに切り詰められるバグ。
// build_node_ternary のポインタ判定が psx_node_is_pointer / FUNCREF のみで、
// ND_ADDR (`&a`) を見ていなかったため結果型が I32 になり、選択したポインタ(8B)が
// ldrsw で下位 32bit に切り詰められて誤アドレスになっていた (`(c?&a:&b)->m`)。
// 修正: 三項の分岐が ND_ADDR なら結果をポインタ (8B slot) として扱う。
// 修正前: exit=139 等 (garbage)
// 期待: exit=42
struct N { int v; };
int main(void) {
    struct N a = {42}, b = {7};
    int c = 1;
    struct N *p = (c ? &a : &b);     // &a
    int via_var = p->v;              // 42 (制御: 変数経由)
    int via_direct = (c ? &a : &b)->v;   // 42 (直接 ->)
    return (via_var == 42 && via_direct == 42) ? 42 : 0;
}
