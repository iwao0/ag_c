// 文字列リテラルへのポインタを s[1] で添字
// 'B' = 66
// 期待: exit=66
int main(void) {
    char *s = "AB";
    return s[1];
}
