// char 配列の短い文字列初期化 (`char a[10] = "hi"`) で残りを 0 で埋める
// 修正前: parse_array_initializer は文字列が配列より短いとき NUL を 1 個
// しか書かず、a[3..9] が未初期化スタック値のままだった (C11 6.7.9p21 違反)。
// `char a[N] = "..."` と `char a[N] = {"..."}` の両形式を修正。
#include <assert.h>
int main(void) {
  char a[10] = "hi"; // 'h','i','\0','\0',...
  assert(a[0] == 'h');
  assert(a[1] == 'i');
  // 文字列より後ろは 0 で埋められること (本バグの核心: zero-fill 不足)
  for (int i = 2; i < 10; i++) assert(a[i] == 0);
  return 0;
}
// 期待: 209
