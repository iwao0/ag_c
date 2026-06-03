// 文字列リテラルへのポインタを *s でデリファレンス
// 'A' = 65
// 期待: exit=65
int main(void) {
    char *s = "AB";
    return *s;
}
