// _Generic の制御式がグローバル変数のときも複雑な派生型を区別する。
// 局所変数と同様、宣言時に型を正規化トークン文字列にして名前で記録し (グローバルは翻訳単位を
// 通じて永続)、_Generic の照合で比較する。
#include <assert.h>

int (*gen_fp)(int, int);
int (*(*gen_np)(void))[3];

int main(void) {
  // 引数の個数で区別する
  assert(_Generic(gen_fp, int (*)(int): 1, int (*)(int, int): 2, default: 0) == 2);
  // 深いネスト型
  assert(_Generic(gen_np, int (*(*)(void))[3]: 7, int *: 9, default: 0) == 7);
  return 0;
}
