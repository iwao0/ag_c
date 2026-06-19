// (int)/(signed)/(unsigned) で long を 32bit に切り詰める処理が抜けており、
// 代入や戻り値では store 幅で偶然合っていたが、インライン比較/演算では 64bit 値の
// まま使われ `(int)0x100000000L == 0` や `(int)long_var == 0` が偽になっていた。
// 定数は値を切り詰め、long 値は (x<<32)>>32 (signed=算術 / unsigned=論理シフト) で
// 低 32bit を 64bit へ拡張して修正。
#include <assert.h>
int main(void) {
  int t = 0;

  // 定数
  t += ((int)0x100000000L == 0);
  t += ((int)0x1FFFFFFFFL == -1);
  t += ((unsigned)0x1FFFFFFFFL == 0xFFFFFFFFu);

  // 変数 (long)
  long a = 0x100000000L;
  t += ((int)a == 0);
  long b = 0x123456789AL;
  t += ((int)b == 0x3456789A);
  long c = 0x1FFFFFFFFL;       // low32 = -1
  t += ((int)c == -1);

  // unsigned long
  unsigned long u = 0x1FFFFFFFFUL;
  t += ((int)u == -1);          // signed result, sign-extend low 32
  t += ((unsigned)u == 0xFFFFFFFFu);

  // 小さい値・負値は不変
  long small = 100, neg = -7;
  t += ((int)small == 100) + ((int)neg == -7);

  assert(t == 10); return 0;  // 10 checks -> 10+32 = 42
}
