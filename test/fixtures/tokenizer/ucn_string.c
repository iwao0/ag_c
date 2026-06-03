// UCN を含む文字列リテラル (basic charset 外)
// é = U+00E9 → 1 byte 0xE9 = 233
// 期待: exit=233
int main(void) {
    char *s = "\u00e9";
    return (int)(unsigned char)s[0];
}
