/* VLA `sizeof(arr)` の variadic 引数経路 (printf 等) 直渡しでの正常評価:
 *   int sz = 7; int arr[sz]; printf("%zu\n", sizeof(arr));  /* 旧: garbage *\/
 *
 * 以前は parse_sizeof_operand が VLA arr の全体サイズスロット (arr_var->offset + 8) を
 * 指す ND_LVAR をそのまま返していた。これは scalar 8B 値のはずだが、IR builder の
 * variadic 引数経路 find_owning_lvar が arr_var (VLA メタ slot サイズ=16) を所属判定し、
 * cg_size_needs_indirect_struct(16) が真となって「struct 16B 値渡し」扱いに化け、2 slot
 * 渡し (各 8B = 16B) で渡されて後続スロットから garbage を拾っていた。中間変数経由
 * (`long s = sizeof(arr); printf("%zu", s)`) は scalar 経路なので壊れない。
 *
 * 修正: VLA 全体サイズ + 行サイズの sizeof 返り値を ND_CAST でラップし、scalar 8B
 * unsigned long として明示。find_owning_lvar の所属判定を回避して variadic 経路で 8B
 * 1 slot として正しく渡される。 */
#include <assert.h>
#include <stdio.h>

int main(void) {
  /* (1) 1D VLA の sizeof 直渡し */
  int sz = 7;
  int arr[sz];
  size_t s1 = sizeof(arr);
  assert(s1 == sz * sizeof(int));
  /* printf 直渡しでも正しく表示される (28 = 7*4) */
  char buf[32];
  snprintf(buf, sizeof(buf), "%zu", sizeof(arr));
  /* sz*4 = 28 を文字列化したものと一致 */
  char expected[16];
  snprintf(expected, sizeof(expected), "%d", sz * 4);
  for (int i = 0; expected[i] || buf[i]; i++) assert(buf[i] == expected[i]);

  /* (2) 2D VLA の行 sizeof (vla_row_stride_frame_off スロット経由) */
  int r = 3, c = 5;
  int mat[r][c];
  size_t s2 = sizeof(mat[0]);
  assert(s2 == c * sizeof(int));
  snprintf(buf, sizeof(buf), "%zu", sizeof(mat[0]));
  snprintf(expected, sizeof(expected), "%d", c * 4);
  for (int i = 0; expected[i] || buf[i]; i++) assert(buf[i] == expected[i]);

  /* (3) 単純な scalar 経由 (回帰確認) */
  size_t s3 = sizeof(sz);
  assert(s3 == sizeof(int));

  return 0;
}
