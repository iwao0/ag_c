// UCN を含む文字列リテラル: narrow char 文字列では UTF-8 にエンコードされる (C11)
// é = U+00E9 → UTF-8 で 2 byte (0xC3 0xA9)
// s[0]=0xC3=195, s[1]=0xA9=169 → 195+169=364 mod 256 = 108
// 期待: exit=108
int main(void) {
    char *s = "\u00e9";
    return (int)((unsigned char)s[0] + (unsigned char)s[1]);
}
