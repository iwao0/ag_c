// char 配列の文字列リテラル初期化 (明示サイズ)
// char s[4] = "abc"; → s = {'a','b','c','\0'}
// 期待: exit=99 ('c'=99, s[3]=0)
int main(void) {
    char s[4] = "abc";
    return s[2] + s[3];
}
