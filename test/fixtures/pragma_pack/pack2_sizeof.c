// pack(2) で char a + 1 byte padding + int b で 6 byte
// 期待: exit=6
#pragma pack(push, 2)
struct S { char a; int b; };
#pragma pack(pop)
int main(void) {
    return (int)sizeof(struct S);
}
