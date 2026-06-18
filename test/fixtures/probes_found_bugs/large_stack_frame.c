// スタックフレームが 4096B を超えると、ag_c が `sub sp, sp, #4112` や
// `add x19, x29, #8576` のような無効な命令を出していたバグ。ARM64 の add/sub 即値は
// imm12 (0..4095) か imm12<<12 (4096 の倍数) しか符号化できず、4095 超は分割が必要。
// 修正前: アセンブルエラー (expected integer in range [0, 4095])
// 修正: emit_addsub_imm が 4096 の倍数部 (lsl #12) と端数に分割 (clang と同じ)。
// 期待: exit=42
#include <assert.h>
int main(void) {
  volatile int a[2000];   // 約 8000B のフレーム (>4096)
  for (int i = 0; i < 2000; i++) a[i] = i * 2;
  // 要素を個別に検査 (フレーム先頭・末尾・中間の局所アドレス計算が >4095 になる)
  assert(a[0] == 0);
  assert(a[1] == 2);
  assert(a[1000] == 2000);
  assert(a[1999] == 3998);
  return 0;
}
