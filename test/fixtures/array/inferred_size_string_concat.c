// 配列サイズ推定: 隣接文字列リテラルの連結を含むケース
// 期待: exit=199 ('a'=97, 'f'=102)
int main(void) {
    char s[] = "abc" "def";
    return s[0] + s[5];
}
