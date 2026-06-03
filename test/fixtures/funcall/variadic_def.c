// 可変引数のみの関数定義 (受理確認)
// 期待: exit=9
int pick(...) { return 9; }
int main(void) { return pick(); }
