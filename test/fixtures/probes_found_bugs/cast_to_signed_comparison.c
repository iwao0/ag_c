// `(int)u` / `(unsigned)i` の明示キャストが終端値ノードの符号フラグを更新せず、
// 後段の比較/除算の符号 (通常算術変換) が誤っていたバグ。
// `int i=-2; unsigned n=5; i < (int)n` が unsigned 比較になり、-2 が巨大な符号なし値
// として扱われ false になっていた (clang は (int)n により signed 比較で true)。
// 原因: apply_cast の (int)/(signed)/(unsigned) 経路が operand の is_unsigned を
//   設定していなかった (is_pointer はクリアしていた)。
// 修正: 終端値ノード (LVAR/GVAR/DEREF/ASSIGN) の is_unsigned を cast の型に合わせる。
//   binop ノード (シフト等) は is_unsigned が LSR/ASR を兼ねるため触れない。
// 期待: exit=42
int main(void) {
    // (int) で unsigned を signed 比較に
    unsigned n = 5;
    int i = -2;
    if (!(i < (int)n)) return 1;            // signed: -2 < 5 = true

    // ループ境界での (int) キャスト
    int s = 0;
    for (int k = -2; k < (int)n; k++) s += k;  // -2..4 = 7
    if (s != 7) return 2;

    // (unsigned) で int を unsigned 比較に
    int neg = -1;
    if (!((unsigned)neg > 0u)) return 3;    // unsigned: 0xFFFFFFFF > 0 = true

    // (int) キャストした unsigned を signed 除算
    unsigned big = 10;
    int d = -3;
    if ((int)big / d != -3) return 4;       // signed: 10 / -3 = -3

    return s + 35;                          // 7 + 35 = 42
}
