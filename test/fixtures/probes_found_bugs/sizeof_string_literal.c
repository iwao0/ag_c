// 文字列リテラルの sizeof (配列だから null 含む)
int main(void) {
  return (int)sizeof("hello");
}
// 期待: 6
