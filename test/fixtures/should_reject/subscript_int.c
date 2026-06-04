// 整数値をサブスクリプトは不正 (`a[i]` は a または i のいずれかが配列/ポインタ)。
// 期待: ag_c はエラー
int main(void) { int x = 5; return x[0]; }
