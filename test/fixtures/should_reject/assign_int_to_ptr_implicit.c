// `int *p = 3;` は整数からポインタへの暗黙変換で不正 (NULL ポインタ定数除く)。
// 期待: ag_c は型不整合でエラー
int main(void) { int *p = 3; return *p; }
