// 条件演算子が返すポインタ/配列への添字 `(c ? a : b)[i]` が E3064 で誤拒否されるバグ。
// psx_node_is_pointer / _deref_size / _type_size が ND_TERNARY を扱わず、
// subscript 判定で「両辺ともポインタ/配列でない」と誤判定していた。
// C11 6.5.15: 条件演算子の結果は両オペランドがポインタならポインタ型。
// 修正前: E3064 でコンパイル失敗
// 期待: exit=42
int main(void) {
    int n = 5;
    char c = (n % 2 ? "odd" : "even")[0];   // "odd"[0] = 'o' = 111
    int a[3] = {7, 8, 9}, b[3] = {40, 41, 42};
    int v = (0 ? a : b)[2];                 // b[2] = 42
    return (c == 'o' && v == 42) ? 42 : 0;
}
