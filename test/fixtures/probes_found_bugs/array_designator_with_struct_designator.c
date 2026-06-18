// 配列 designator `[N]=` の中に struct designator `.a=` をネスト
// 修正前: parse_array_elem_struct_brace_init は struct 配列要素の brace 初期化を
// `tag_get_next_named_member` で member 順序通りにしか処理しておらず、
// `{.a=1, .b=2}` 形式 (struct designator) では `.a` が expr として
// 評価されてエラー (E3064: [primary] 数値が必要です)。
//
// 修正: parse_array_elem_struct_brace_init を `parse_struct_initializer` に
// 委譲。配列要素の offset (var->offset + idx * elem_size) と要素 1 つ分の
// size を nested lvar にセットして呼び出すだけで、designator も positional
// も両形に対応する。zero-fill 範囲も要素 1 つに制限される。
#include <assert.h>
struct V { int a; int b; };
int main(void) {
  struct V arr[3] = {
    [0] = {.a = 1, .b = 2},
    [2] = {.a = 10, .b = 20}
    // arr[1] = {0, 0} (zero-fill)
  };
  assert(arr[0].a == 1);
  assert(arr[0].b == 2);
  assert(arr[1].a == 0);   // zero-fill
  assert(arr[1].b == 0);
  assert(arr[2].a == 10);
  assert(arr[2].b == 20);
  return 0;
}
// 期待: 33
