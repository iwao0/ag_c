// `(int)long_returning_fn()` が切り詰められなかった。parser は関数戻り値の型幅を
// 覚えておらず funcall の type_size を 4 と推定するが、long 戻り値は x0 に 64bit で
// 返るため、`(int)bigval() == 7` が 64bit 比較で偽になっていた。
// (int)/(unsigned) キャストの切り詰め対象に ND_FUNCALL を含めて修正
// (int 戻り値関数でも低 32bit 抽出は無害)。
#include <assert.h>
long bigval(void){ return 0x100000007L; }      // low32 = 7
long negval(void){ return 0x1FFFFFFFFL; }       // low32 = -1
int geti(void){ return -42; }
char getch(void){ return 'A'; }
unsigned getu(void){ return 0xFFFFFFFFu; }

int main(void) {
  int t = 0;

  // long 戻り値の (int) 切り詰め
  t += ((int)bigval() == 7);
  t += ((int)negval() == -1);
  t += ((unsigned)bigval() == 7u);

  // int / char / unsigned 戻り値も従来通り正しい
  t += ((int)geti() == -42);
  t += ((int)getch() == 65);
  t += ((int)getu() == -1);
  t += ((unsigned)geti() == 4294967254u);

  // キャスト無しの long 戻り値は 64bit のまま正しい
  t += (bigval() == 0x100000007L);
  t += (bigval() + bigval() == 0x20000000EL);

  assert(t == 9); return 0;  // 9 checks -> 9+33 = 42
}
