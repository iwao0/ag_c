// _Bool 配列の要素代入で 0/1 正規化
// 修正前: ローカル `_Bool flags[N]` の各要素代入 (`flags[i] = 100`) で
// 0/1 正規化が抜けていた。スカラ `_Bool b = 100` は機能していたが、
// 配列の場合は build_subscript_deref が pointee_is_bool を伝播せず、
// 結果の ND_DEREF に is_bool=1 が立たないため、assign 経路の正規化が走らない。
//
// 配列ベース ND_ADDR に pointee_is_bool を立てる経路と、
// build_subscript_deref で deref->is_bool に引き継ぐ経路を追加した。
#include <assert.h>
int main(void) {
  _Bool flags[5] = {1, 0, 1, 1, 0};
  assert(flags[0] == 1);
  assert(flags[1] == 0);
  assert(flags[2] == 1);
  assert(flags[3] == 1);
  assert(flags[4] == 0);
  flags[1] = 100;            // 100 → 1 に正規化されるべき (本バグの核心)
  assert(flags[1] == 1);
  return 0;
}
// 期待: 13
