// 不完全 struct へのポインタ宣言
// 期待: exit=1
int main(void) {
    struct S;
    struct S *p;
    p = 0;
    return p == 0;
}
