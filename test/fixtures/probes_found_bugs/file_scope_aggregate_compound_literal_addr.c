/* ファイルスコープの struct/union/配列 複合リテラルのアドレス `&(struct S){...}` (C11 6.5.2.5)。
 * 単一スカラ初期化子 (`&(int){5}`) しか扱えず、`&(struct S){3,4}` が brace の `,` で E2006 に
 * なっていた。グローバル struct/配列と同じ psx_parse_global_brace_init_flat で gvar 実体へ
 * 展開してアドレスを返すよう、ファイルスコープ経路に集約 (struct/union/配列) 分岐を追加。 */
#include <assert.h>

struct S { int a, b; };
struct P { int x; struct S s; };   /* ネスト struct */
union U { int n; long l; double d; };

struct S *p_struct = &(struct S){3, 4};
struct S *p_desig  = &(struct S){.b = 7};
int *p_arr         = &(int[3]){10, 20, 30}[0];
struct P *p_nested = &(struct P){1, {2, 3}};
union U *p_union   = &(union U){.l = 0x1122334455L};
char *p_str        = (char[6]){"hi"};   /* 文字配列複合リテラル */
double *p_darr     = &(double[2]){1.5, 2.5}[0];

int main(void){
  assert(p_struct->a == 3 && p_struct->b == 4);
  assert(p_desig->a == 0 && p_desig->b == 7);
  assert(p_arr[0] == 10 && p_arr[1] == 20 && p_arr[2] == 30);
  assert(p_nested->x == 1 && p_nested->s.a == 2 && p_nested->s.b == 3);
  assert(p_union->l == 0x1122334455L);
  assert(p_str[0] == 'h' && p_str[1] == 'i' && p_str[2] == 0);
  assert(p_darr[0] == 1.5 && p_darr[1] == 2.5);
  return 0;
}
