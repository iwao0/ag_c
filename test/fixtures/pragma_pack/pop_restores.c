// push/pop で push 前の alignment が復元されること
// pop 後の struct は通常の alignment で 8 byte
// 期待: exit=8
#pragma pack(push, 1)
#pragma pack(pop)
struct S { char a; int b; };
int main(void) {
    return (int)sizeof(struct S);
}
