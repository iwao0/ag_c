// `int x = "hello";` は文字列リテラル (char *) を int に代入しており不正。
// 期待: ag_c は型不整合でエラー (cc は incompatible pointer to integer conversion)
int main(void) { int x = "hello"; return x; }
