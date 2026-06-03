// ビットフィールドへの書き込みと a 側の読み出し
// 期待: exit=5
int main(void) {
    struct S { unsigned int a:3; unsigned int b:5; };
    struct S s;
    s.a = 5;
    s.b = 10;
    return s.a;
}
