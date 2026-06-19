// struct 配列を仮引数で受ける `struct V arr[]` → `struct V *arr` に adjust
// 修正前: parse_param_decl の分岐が `struct V arr[]` を ≤16B 値渡し経路に
// 流していたため、`arr[i].x` の subscript で「両辺ポインタじゃない」と
// 判定されエラー (E3064)。
//
// param_is_array_declarator && tag_kind != EOF のとき struct ポインタ扱い
// (is_tag_pointer=1, size=8, elem_size=struct_size) で登録する経路を追加。
#include <assert.h>
struct V { int v; };
int sum(struct V arr[], int n) {
  int s = 0;
  for (int i = 0; i < n; i++) s += arr[i].v;
  return s;
}
int main(void) {
  struct V data[4] = {{10}, {20}, {30}, {40}};
  assert(sum(data, 4) == 100); return 0; // 100
}
// 期待: 100
