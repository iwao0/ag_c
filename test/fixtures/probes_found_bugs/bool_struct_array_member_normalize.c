// struct メンバの _Bool 配列要素代入で 0/1 正規化
// 修正前: 直前の bool_array_element_normalize 修正で「ローカル _Bool 配列」は
// 直ったが、struct メンバが _Bool 配列の場合 (s.flags[i] = 200) は依然として
// 正規化が抜けていた。
// build_member_access で _Bool メンバを deref->is_bool に書いていたため、
// 配列メンバ (= deref が配列ベース = ポインタ扱い) では subscript 結果に
// 引き継がれなかった。配列メンバなら pointee_is_bool に書くよう分岐。
#include <assert.h>
struct S { _Bool flags[4]; int n; };
int main(void) {
  struct S s = { {1, 0, 1, 0}, 5 };
  s.flags[1] = 200;            // 1 に正規化されるべき (本バグの核心)
  assert(s.flags[0] == 1);
  assert(s.flags[1] == 1);     // 200 -> 1
  assert(s.flags[2] == 1);
  assert(s.flags[3] == 0);
  assert(s.n == 5);
  return 0;
}
// 期待: 35
