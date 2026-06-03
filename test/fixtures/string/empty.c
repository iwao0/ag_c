// 空文字列リテラルは NUL のみ
// 期待: exit=0
int main(void) {
    char *s = "";
    return *s;
}
