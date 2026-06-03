// 最小のタグ宣言 (struct/union/enum のみ)
// 期待: exit=7
int main(void) {
    struct S;
    union U;
    enum E;
    return 7;
}
