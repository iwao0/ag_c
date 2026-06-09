// グローバル char 配列を文字列で初期化
char msg[] = "hi";
int main(void) {
  return msg[0] + msg[1];
}
// 期待: 'h'(104) + 'i'(105) = 209
