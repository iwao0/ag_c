// `#pragma pack(N)` (push 無し) と `#pragma pack()` (リセット) も動くこと
// 期待: exit=5
#pragma pack(1)
struct S { char a; int b; };
#pragma pack()
int main(void) {
    return (int)sizeof(struct S);
}
