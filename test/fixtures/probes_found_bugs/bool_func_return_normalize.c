// _Bool 戻り値関数で `return 200;` を 0/1 に正規化
// 修正前: parser の return 文は戻り値型 _Bool を考慮せず、return 式をそのまま
// 返していた。caller が `flag * 7` のように整数演算に使うと 200*7=1400 が
// 返り、(int) 経由で見ると 130 等の garbage 化していた。
//
// semantic pass の return 変換で、戻り値型が _Bool の場合に `lhs != 0` を被せて
// 0/1 に正規化する。
#include <assert.h>
_Bool always_big(int x) { (void)x; return 200; }
_Bool is_pos(int x) { return x > 0; }
int main(void) {
  assert(is_pos(5));            // true
  assert(!is_pos(-3));          // false
  assert(always_big(0) == 1);   // 200 が _Bool 戻りで 0/1 に正規化される
  return 0;
}
// 期待: 17
