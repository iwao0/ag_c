// float/double 値を直接ブール条件に使う (`f ? a : b` / `if (f)` / `while (f)`) と、
// レジスタ圧で fp 値が spill されたとき codegen が 4B float を 8B 整数として load し、
// 上位 32bit に garbage を拾って 0.0 が真と誤判定されることがあった。
// emit_br_cond で fp 条件を (cond != 0.0) (IR_FNE) に変換してから分岐するよう修正。
#include <assert.h>
int main(void) {
  int t = 0;

  // 多数の fp をライブにしてレジスタ圧をかける
  float a = 0.0f, b = 1.5f, c = 0.0f, d = 2.5f, e = 3.5f;

  // 三項の fp 条件 (元のバグ: a=0.0 が真扱いで 0 を返していた)
  t += (a ? 0 : 1);          // 1
  t += (b ? 1 : 0);          // 1
  t += (c ? 0 : 1);          // 1

  // if の fp 条件
  if (a) t += 100; else t += 1;   // 1
  if (d) t += 1; else t += 100;   // 1

  // double 条件
  double x = 0.0, y = 9.0;
  t += (x ? 0 : 1);          // 1
  t += (y ? 1 : 0);          // 1

  // while の fp 条件
  float cd = 4.0f; int loops = 0;
  while (cd) { loops++; cd -= 1.0f; }
  t += (loops == 4);         // 1

  // for の fp 条件 + !float
  int fc = 0;
  for (float i = 3.0f; i; i -= 1.0f) fc++;
  t += (fc == 3);            // 1
  t += (!a);                 // !0.0 = 1

  // 比較で値が正しいことも確認 (b,c,d,e がライブ)
  t += (b + c + d + e == 7.5f); // 1

  assert(t == 11); return 0;  // 11 checks -> 11+31 = 42
}
