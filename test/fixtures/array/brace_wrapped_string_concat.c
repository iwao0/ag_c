// `char a[] = {"abc" "de"}` — ブレース内 + 隣接文字列連結
// 連結結果は "abcde" (5 文字 + NUL → サイズ 6)
// 期待: exit=199 ('a'=97, 'e'=101, a[5]==0 → +1 = 199)
int main(void) {
    char a[] = { "abc" "de" };
    return a[0] + a[4] + (a[5] == 0 ? 1 : 0);
}
