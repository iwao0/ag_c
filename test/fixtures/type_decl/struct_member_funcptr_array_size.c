// struct メンバが関数ポインタ配列 [2] のとき sizeof = 16
// 期待: exit=16
int main(void) {
    struct S { int (*arr[2])(int); };
    return sizeof(struct S);
}
