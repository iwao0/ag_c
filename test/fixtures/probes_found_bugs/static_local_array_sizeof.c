/* static ローカル配列の sizeof が要素サイズを返していたバグ (C11 6.5.3.4)。
 * `static int a[10]` は try_lower_static_local_array でグローバルへ lowering され、
 * alias lvar は is_array=0 / size=0 で登録される。parse_sizeof_operand の配列特別処理は
 * arr_var->is_array でガードしているため static ローカル配列を素通りし、一般経路で
 * ND_ADDR(ND_GVAR) の type_size=要素ストライド(4) を返していた (sizeof=4)。実サイズは
 * lowering 先グローバルの type_size にあるので、static ローカル配列のときはそれを返す。 */
#include <assert.h>

int int_arr_sizeof(void){ static int a[10]; return (int)sizeof(a); }
int char_arr_sizeof(void){ static char c[7]; return (int)sizeof(c); }
int double_arr_sizeof(void){ static double d[4]; return (int)sizeof(d); }
unsigned long elem_count(void){ static long L[5]; return sizeof(L) / sizeof(L[0]); }

int main(void){
  assert(int_arr_sizeof() == 40);     /* 10 * 4 */
  assert(char_arr_sizeof() == 7);     /* 7 * 1 */
  assert(double_arr_sizeof() == 32);  /* 4 * 8 */
  assert(elem_count() == 5);          /* sizeof(arr)/sizeof(elem) idiom */
  /* 値も正しく読めること (lowering 先が壊れていない確認) */
  static int v[3] = {11, 22, 33};
  assert(sizeof(v) == 12);
  assert(v[2] == 33);
  return 0;
}
