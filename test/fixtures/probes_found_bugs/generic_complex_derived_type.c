// _Generic の複雑な派生型の照合 (C11 6.5.1.1)。
// 回帰対象: 関数ポインタの引数リスト (`int(*)(int)` vs `int(*)(int,int)`) や深いネスト型
// (`int(*(*)(void))[3]`) を generic_type_t の構造的フィールドでは区別できず、前者を誤マッチ
// /後者を default に落としていた。型を正規化トークン文字列にして照合するよう修正。
#include <assert.h>

int main(void) {
  int (*f1)(int) = 0;
  int (*f2)(int, int) = 0;
  // 引数の個数で区別する
  assert(_Generic(f1, int (*)(int): 1, int (*)(int, int): 2, default: 0) == 1);
  assert(_Generic(f2, int (*)(int): 1, int (*)(int, int): 2, default: 0) == 2);

  // 深いネスト型: 関数(void)を指すポインタへのポインタが指す int[3] 配列へのポインタ
  int (*(*p)(void))[3] = 0;
  assert(_Generic(p, int (*(*)(void))[3]: 7, int *: 9, default: 0) == 7);

  // 引数の型で区別する (個数が同じでも別型)
  double (*g1)(int) = 0;
  assert(_Generic(g1, int (*)(int): 1, double (*)(int): 5, default: 0) == 5);

  return 0;
}
