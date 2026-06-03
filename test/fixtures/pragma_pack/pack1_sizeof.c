// #pragma pack(push, 1) で struct のパディングが消えること
// `struct S { char a; int b; }` は通常 8 バイトだが pack(1) で 5 バイト。
// 期待: exit=5
#pragma pack(push, 1)
struct S { char a; int b; };
#pragma pack(pop)
int main(void) {
    return (int)sizeof(struct S);
}
