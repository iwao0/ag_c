// pack(1) でメンバ b のオフセットが詰まっても値の読み書きが正しいこと
// 期待: exit=42 (1 + 41)
#pragma pack(push, 1)
struct S { char a; int b; };
#pragma pack(pop)
int main(void) {
    struct S s;
    s.a = 1;
    s.b = 41;
    return s.a + s.b;
}
