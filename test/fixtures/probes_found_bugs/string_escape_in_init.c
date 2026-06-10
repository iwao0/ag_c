// エスケープシーケンス
int main(void) {
  char s[] = "\t\n\\";
  return s[0] + s[1] + s[2];  // 9 + 10 + 92 = 111
}
// 期待: 111
