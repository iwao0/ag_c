// 符号付き short/char へのキャストはインライン使用でも符号拡張する。
// 旧実装は `& 0xffff` / `& 0xff` のマスク (ゼロ拡張) のみで、代入経由 (store+ldrsh/ldrsb)
// では偶然合っていたが、定数畳み込みや変数を介さない比較/演算で誤った正値になっていた。
// 例: (short)40000 は -25536、(char)200 は -56 (この ABI では char は符号付き)。
#include <assert.h>
int main(void) {

  // 定数の inline short/char キャスト (定数畳み込み経路)
  assert((short)40000 == -25536);
  assert((char)200 == -56);
  assert((char)300 == 44);          // 300 -> 44 (正値、bit7 立たず)
  assert((short)0x12345678 == 0x5678);

  // runtime 値の short/char キャスト (変数を介さず inline 比較)
  int x = 40000, y = 200;
  assert((short)x == -25536);
  assert((char)y == -56);

  // 複合代入で short にラップした値を inline でキャスト比較
  short s = 30000; s += 10000;          // -25536
  assert(s == (short)40000);

  // unsigned char/short はゼロ拡張のまま (符号拡張しない)
  assert((unsigned short)40000 == 40000u);
  assert((unsigned char)200 == 200u);
  assert((unsigned short)x == 40000u);
  assert((unsigned char)y == 200u);

  // unsigned short の最上位ビットが立った値が負にならないこと
  unsigned short us = 0xFFFF;
  assert((unsigned short)us == 65535u);

  return 0;
}
