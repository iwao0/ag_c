// char 配列の短い文字列初期化 (`char a[10] = "hi"`) で残りを 0 で埋める
// 修正前: parse_array_initializer は文字列が配列より短いとき NUL を 1 個
// しか書かず、a[3..9] が未初期化スタック値のままだった (C11 6.7.9p21 違反)。
// `char a[N] = "..."` と `char a[N] = {"..."}` の両形式を修正。
int main(void) {
  char a[10] = "hi"; // 'h','i','\0','\0',...
  int s = 0;
  for (int i = 0; i < 10; i++) s += a[i];
  return s; // 'h'+'i' = 209
}
// 期待: 209
