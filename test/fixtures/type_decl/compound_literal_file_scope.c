// ファイルスコープの int 複合リテラル
// 期待: exit=42
int x = (int){42};
int main(void) { return x; }
