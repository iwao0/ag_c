// 配列サイズ推定: char s[] = "hello"
// 修正前: E3064 "数値が必要です ']'" でコンパイル失敗
// 期待: exit=215 ('h'=104, 'o'=111)
int main(void) {
    char s[] = "hello";
    return s[0] + s[4];
}
