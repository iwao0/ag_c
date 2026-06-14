// `int **pp` の subscript 結果 `pp[i]` の pointer_qual_levels が減算されず、
// `*pp[i]` (二重 deref) がスカラなのにポインタ扱いされていたバグ。
// pp[i] は int* (pql=1) のはずが pql=2 のままで、`*pp[i]` の deref 結果が
// pql=1 (ポインタ) になり、`int u = *pp[0];` のスカラ初期化が E3064 で拒否され、
// `*pp[0] + *pp[1]` の算術も pointer 化して値がスケールされていた (3+10 が 3+40)。
// 修正: genuine ポインタ変数 (ND_LVAR/ND_GVAR) の subscript は 1 段消費するので
//   結果 pql を 1 減らす。配列 (`int *arr[N]`, ND_ADDR decay) は配列次元を消費し
//   要素の pql を保つ。
// 修正前: `int u=*pp[0];` が E3064 / `*pp[0]+*pp[1]` が誤値
// 期待: exit=42
int main(void) {
    int arr[3] = {10, 20, 12};
    int x = 3;
    int *pa[2] = {&x, arr};
    int **pp = pa;

    // 二重 deref のスカラ初期化 (修正前 E3064)
    int u = *pp[0];          // *(&x) = 3
    int v = *pp[1];          // *arr  = 10
    if (u != 3 || v != 10) return 1;

    // 算術でも pointer スケールされない (修正前 3 + 10*4 = 43)
    int s = *pp[0] + *pp[1]; // 3 + 10 = 13
    if (s != 13) return 2;

    // 三重ポインタも段数どおり
    int w = 7;
    int *p3 = &w;
    int **pp3 = &p3;
    int ***ppp = &pp3;
    int t = **ppp[0];        // = w = 7
    if (t != 7) return 3;

    return u + v + s + t + 9;   // 3 + 10 + 13 + 7 + 9 = 42
}
