// ストリーミング字句解析の前方先読み境界バグの回帰。
// _Generic の型照合はカーソルを進めずに t->next で型全体を先読みする。ストリーミング生成器は
// カーソル前方を一定窓だけ materialize し、set_curtok のジャンプが補充起点を飛び越えると補充が
// チェーン末尾到達まで起きない。複雑なグローバル funcptr/ネスト宣言 + 複数の assert(_Generic)
// (assert の #expr 文字列化で大量トークン) + 末尾の cast 制御式の深いネスト型が同居すると、
// 窓境界が型の途中に来て t->next が未生成境界(NULL)を踏み、有効な型を誤って却下し E2006 を出す。
// 修正: 深い前方先読みの直前に tk_ensure_lookahead() で窓を満たす。
// (このファイルは修正前パース不能だった。全 assert 成功で main は 0 を返す。)
#include <assert.h>

int (*gen_fp)(int, int);
int (*(*gen_np)(void))[3];

int main(void) {
  assert(_Generic(gen_fp, int (*)(int): 1, int (*)(int, int): 2, default: 0) == 2);
  assert(_Generic(gen_np, int (*(*)(void))[3]: 7, int *: 9, default: 0) == 7);
  int (*f1)(int) = 0;
  int (*f2)(int, int) = 0;
  assert(_Generic(f1, int (*)(int): 1, int (*)(int, int): 2, default: 0) == 1);
  assert(_Generic(f2, int (*)(int): 1, int (*)(int, int): 2, default: 0) == 2);
  int (*(*p)(void))[3] = 0;
  assert(_Generic(p, int (*(*)(void))[3]: 7, int *: 9, default: 0) == 7);
  double (*g1)(int) = 0;
  assert(_Generic(g1, int (*)(int): 1, double (*)(int): 5, default: 0) == 5);
  assert(_Generic((int (*)(int, int))0, int (*)(int): 1, int (*)(int, int): 2, default: 0) == 2);
  assert(_Generic((int (*(*)(void))[3])0, int (*(*)(void))[3]: 7, int *: 9, default: 0) == 7);
  return 0;
}
