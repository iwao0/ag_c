// グローバル配列 `int g[] = {...}` の sizeof で全体サイズを返す
// 修正前: parse_sizeof_operand はローカル配列のみ「array → ptr decay しない」
// 特殊経路を持ち、グローバル配列は普通の式評価に流れて ND_ADDR の type_size
// (= ストライド/要素サイズ) を見て sizeof = 4 を返していた。
// 結果 sizeof(g)/sizeof(g[0]) = 4/4 = 1 と誤算。
//
// グローバル `gv->is_array && gv->type_size > 0` の場合に
// gv->type_size を返す経路を追加。要素数推定 (`int g[] = {1,2,3}`) は
// apply_toplevel_object_initializer 側で確定済み。
int g[] = {10, 20, 30};
int main(void) {
  return (int)(sizeof(g) / sizeof(g[0])); // 3
}
// 期待: 3
