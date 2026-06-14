// struct/union ポインタ仮引数の subscript `a[i]` が、構造体サイズではなく常に 8 で
// スケーリングされていたバグ。register_param_lvar が struct ポインタ仮引数の
// deref_size を 8 にハードコードしていたため、4 バイト構造体 (int 1個) の `a[i]` が
// a + i*8 と誤計算され、隣の要素や範囲外を読んでいた (8 バイト構造体は偶然一致)。
// 修正: 単段ポインタ仮引数の pointee を struct_size にする。
// 修正前: a[2].v が garbage
// 期待: exit=42
struct N { int v; };          // 4 バイト構造体
int get(struct N *a, int i) { return a[i].v; }
struct N *getp(struct N *a, int i) { return &a[i]; }
int main(void) {
    struct N arr[3] = {{10}, {20}, {12}};
    return get(arr, 2) + getp(arr, 0)->v + get(arr, 1);   // 12 + 10 + 20 = 42
}
