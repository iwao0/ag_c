// __func__ で関数名の先頭文字を取得 ('m' == 109)
// 期待: exit=109
int main(void) {
    return (int)__func__[0];
}
