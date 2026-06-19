// グローバル double 配列の初期化
// 修正前: init_values[] が long long で fval を格納できず、
// .quad 0 連続として出力されていた。配列要素読み出し時にも要素型 fp_kind が
// pointee_fp_kind に伝播しておらず、整数 load になっていた。
#include <assert.h>
double tbl[5] = {1.5, 2.5, 3.5, 4.5, 5.5};
int main(void) {
  double s = 0;
  for (int i = 0; i < 5; i++) s += tbl[i];
  assert((int)s == 17); return 0; // 17.5 → 17
}
// 期待: 17
