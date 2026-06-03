// 指定初期化子 + 添字
// 期待: exit=99 ([2]=99)
int main(void) { return ((int[4]){[2] = 99})[2]; }
