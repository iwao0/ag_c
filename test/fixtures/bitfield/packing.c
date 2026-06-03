// ビットフィールド a:3 + b:5 が 1 バイトに収まり、後続 int との合計 sizeof=8
// (struct アライメント 4、ビットフィールドが 1 バイト、padding 3 バイト、int 4 バイト)
// 期待: exit=8
int main(void) {
    struct S { unsigned int a:3; unsigned int b:5; int c; };
    return (int)sizeof(struct S);
}
