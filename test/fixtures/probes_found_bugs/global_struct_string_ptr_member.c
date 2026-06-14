// グローバル構造体/構造体配列の char* メンバを文字列リテラルで初期化すると
// ポインタメンバが null/ゴミになるバグ。
// (1) emit_global_struct_init は sym_len>0 のシンボルのみ出力し、文字列リテラル
//     ラベル (sym_len=-1 の sentinel) を数値 0 に落としていた。
// (2) emit_global_struct_array_init は init_value_symbols を一切見ず、常に数値
//     出力していたため、構造体配列のポインタメンバが全滅していた。
// 修正: 両方で sym_len<0 (文字列ラベル) / sym_len>0 (シンボル) を .quad 出力。
// 修正前: exit=139 等 (garbage)
// 期待: exit=42
struct S { char *name; int id; };
struct S one = {"hi", 5};
struct S arr[2] = {{"ab", 1}, {"cd", 2}};
int main(void) {
    // one.name[0]='h'(104), arr[0].name[0]='a'(97), arr[1].name[0]='c'(99)
    int ok = (one.name[0] == 'h' && one.id == 5 &&
              arr[0].name[0] == 'a' && arr[1].name[0] == 'c' &&
              arr[1].id == 2);
    return ok ? 42 : 0;
}
