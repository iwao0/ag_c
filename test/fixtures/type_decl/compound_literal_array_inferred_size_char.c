// char 配列複合リテラル (要素列指定、 NUL 終端)
// 'a'+'b' = 97+98 = 195
// 期待: exit=195
int main(void) {
    char *s = (char[]){'a', 'b', 'c', '\0'};
    return s[0] + s[1];
}
