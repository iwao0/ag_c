/* 混在幅の比較 (片側 i32・片側 i64)。bug_coverage で ⚠️ (gen_inst_int_cmp は両 i32 のみ
 * 32bit) とされていたが、混在は 64bit で sign/zero-extend して比較され clang と一致する
 * (= miscompile ではない)。回帰として固定する。 */
#include <assert.h>

int main(void){
  long a = 5000000000L;          /* > INT_MAX, i64 */
  int b = 100;                   /* i32 */
  assert(a > b);
  assert(!(a < b));

  long neg = -1L;
  unsigned int ui = 1u;
  assert(neg < ui);              /* long vs unsigned int: ui->long, -1 < 1 */

  int si = -1;
  unsigned long ul = 1UL;
  assert(si > ul);               /* int vs unsigned long: -1->0xFFF.. > 1 */

  char c = -1;
  long L = 0;
  assert(c < L);                 /* sub-int vs long */

  unsigned char uc = 255;
  long L2 = 256;
  assert(uc < L2);

  int x = 1000000;
  long y = x * 1000000L;         /* 10^12, i64 で計算 */
  assert(y == 1000000000000L);
  assert(y > x);
  return 0;
}
