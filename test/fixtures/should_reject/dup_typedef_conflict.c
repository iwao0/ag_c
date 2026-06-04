// 異なる型に対する typedef 名の再宣言は不正 (C11 6.7p3, 6.7.8)。
// 期待: ag_c は型衝突エラー
typedef int X;
typedef long X;
int main(void) { return 0; }
