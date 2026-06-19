// `(long)unsigned_int` が arithmetic で zero-extend されず、`(long)u + (long)u` 等が
// 32bit で計算されて符号なし 2^32 ラップマスクで切り詰められていた
// (`3e9 + 3e9` が 6e9 でなく 6e9-2^32 に)。`(long)` キャストは type_size を 8 に
// 広げない no-op で、二項演算の result_ty が I32 になるのが原因。
// `(long)unsigned` を ND_PTR_CAST(widen_zext_i64) でラップし IR_ZEXT を明示挿入して修正
// (signed の (long) は coerce の SEXT で従来通り正しい)。
#include <assert.h>
int main(void) {
  int t = 0;

  // unsigned int を (long) して加算 (2^32 超)
  unsigned u = 3000000000u;
  t += ((long)u + (long)u == 6000000000L);

  unsigned a = 4000000000u, b = 4000000000u;
  t += ((long)a + (long)b == 8000000000L);
  t += ((long)a * 3 == 12000000000L);

  // unsigned char / short to long
  unsigned short us = 60000;
  t += ((long)us * 100000 == 6000000000L);

  // 単一キャストの代入 (zero-extend)
  unsigned x = 0xFFFFFFFFu;
  long lx = (long)x;
  t += (lx == 4294967295L);

  // signed (long) は符号拡張で従来通り正しい
  int s = -1000000;
  t += ((long)s * 1000000 == -1000000000000L);
  int s2 = 2000000000;
  t += ((long)s2 + (long)s2 == 4000000000L);

  // (int)(long)u の往復 (truncate)
  t += ((int)((long)u) == (int)3000000000u);

  assert(t == 8); return 0;  // 8 checks -> 8+34 = 42
}
