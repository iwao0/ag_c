// 同一スコープでローカル変数を重複宣言は不正 (C11 6.7p3)。
// 期待: ag_c は重複宣言エラー
int main(void) { int x = 1; int x = 2; return x; }
