// U"..." (UTF-32) 文字列リテラルで BMP 外の UCN を 1 word に保持する。
// U+1F600 = 0x0001F600 を .word で出力。little endian で bytes は:
// 00 F6 01 00 00 00 00 00 (3 word: code point + 終端)
// 期待: s[1] = 0xF6 = 246
int main(void) {
    char *s = (char*)U"\U0001F600";
    return (int)(unsigned char)s[1];
}
