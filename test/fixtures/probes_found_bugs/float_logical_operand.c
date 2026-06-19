// `&&` / `||` の浮動小数オペランドが整数 NE で 0/1 正規化されていたため、float の bit を
// 整数として比較し、spill 時に上位 garbage を拾って 0.0 が真と誤判定されることがあった。
// build_node_logand_or の右辺正規化を emit_truthiness (fp は FNE against 0.0) に統一して修正。
#include <assert.h>
int main(void) {
  int t = 0;

  // レジスタ圧をかけるため複数の fp をライブに
  float a = 0.0f, b = 1.5f, c = 2.5f, d = 0.0f, e = 3.5f;

  t += ((a && b) == 0);   // 0.0 && _ -> 0
  t += ((b && c) == 1);   // 両方非0 -> 1
  t += ((a || b) == 1);   // 0 || 1.5 -> 1
  t += ((a || d) == 0);   // 0 || 0 -> 0 (元のバグ: 真になっていた)
  t += ((b || a) == 1);   // 1.5 || _ -> 1
  t += ((d && e) == 0);   // 0 && _ -> 0

  // 短絡の副作用なし
  int side = 0;
  int r = (a && (side = 99));   // a=0 で短絡、side 不変
  t += (r == 0) + (side == 0);
  int r2 = (b || (side = 88));  // b!=0 で短絡、side 不変
  t += (r2 == 1) + (side == 0);

  // double オペランド
  double x = 0.0, y = 7.0;
  t += ((x || y) == 1);   // 0 || 7 -> 1
  t += ((x && y) == 0);   // 0 && _ -> 0

  assert(t == 12); return 0;  // 12 checks -> 12+30 = 42
}
