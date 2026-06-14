// 入れ子三項演算子で内側の long 分岐が 32bit に切り詰められていたバグ。
// `a ? x : (b ? bigLong : y)` で内側三項は正しく i64 を返すが、外側三項の結果型が
// i32 と誤判定され 8 バイト値が 4 バイト slot へ切り詰められていた。
// 原因: ternary_branch_is_wide_int が psx_node_type_size を見るが、NUM リテラルは
//   0 とみなされるため、内側三項のリテラル分岐 (100000000000L 等) を拾えなかった。
// 修正: 入れ子三項へ再帰してリテラルの 64bit 分岐を検出する。
// 修正前: 内側 long 分岐が下位 32bit に化ける
// 期待: exit=42
int main(void) {
    int x = 5;
    // 外側 false → 内側 true → 100000000000L (40bit)
    long r = x > 10 ? 1L : x > 3 ? 100000000000L : 2L;
    if (r != 100000000000L) return 1;

    // さらに深い入れ子
    int y = 2;
    long s = y == 0 ? 1L
           : y == 1 ? 2L
           : y == 2 ? 5000000000L
           : 3L;
    if (s != 5000000000L) return 2;

    // 内側が変数の long
    long big = 9000000000L;
    long t = x > 100 ? 0L : x > 50 ? 1L : big;
    if (t != 9000000000L) return 3;

    return (int)(r / 100000000000L) + (int)(s / 5000000000L) + (int)(t / 9000000000L) + 39;  // 1+1+1+39 = 42
}
