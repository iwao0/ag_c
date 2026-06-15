// char/short を (int)/(signed)/(unsigned) へ明示キャストすると、operand の
// is_unsigned が load 符号拡張 (ldrsh/ldrh) も兼ねるため上書きで値が化けていた。
//   (unsigned)(short)-1        が 65535 (正: 4294967295)
//   (int)(unsigned short)0xFFFF が -1    (正: 65535)
// さらに符号混在のインライン比較/除算では sub-int が UAC で signed 昇格扱いになり
// 符号が誤っていた。sub-int の load 符号性を保ちつつ (unsigned) は & 0xffffffff で
// 32bit unsigned へ折り返すことで両方を解決する。
int main(void) {
  int t = 0;

  short s = -1;
  unsigned u1 = (unsigned)s;        // 4294967295
  t += (u1 == 4294967295u);         // +1

  unsigned short us = 0xFFFF;
  int i1 = (int)us;                 // 65535
  t += (i1 == 65535);               // +1

  signed char sc = (signed char)0xFF; // -1
  unsigned u2 = (unsigned)sc;       // 4294967295
  t += (u2 == 4294967295u);         // +1

  // インライン符号混在: (unsigned)(short)-1 は 4294967295 > 5
  t += ((unsigned)s > 5);           // +1

  // インライン除算: 4294967295 / 2 = 2147483647 > 1e9
  t += ((unsigned)s / 2u > 1000000000u); // +1

  // signed ターゲットは値・符号とも従来通り正しい
  signed v = (signed)s;             // -1
  t += (v == -1);                   // +1

  // (unsigned) の unsigned short はそのまま 65535 (< 100000)
  t += ((unsigned)us < 100000u);    // +1

  return t * 6;                     // 7 * 6 = 42
}
