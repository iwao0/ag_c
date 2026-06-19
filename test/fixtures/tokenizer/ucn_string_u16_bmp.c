// u"..." (U+00FF) は単一 hword で出力される。
// 修正後の `gen_string_literals` では v < 0x10000 のとき .hword v、
// それ以上はサロゲートペアに分岐する。U+00FF は分岐前なので 0x00FF 1 個。
// little endian で bytes: FF 00
// s[0] + s[1] = 0xFF + 0x00 = 255
// 期待: exit=255
#include <assert.h>
int main(void) {
    char *s = (char*)u"\u00FF";
    assert((unsigned char)s[0] == 0xFF);
    assert((unsigned char)s[1] == 0x00);
    return 0;
}
