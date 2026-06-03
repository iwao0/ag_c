// union と enum の同時定義
// 期待: exit=7
int main(void) {
    union U { int x; char y; };
    enum E { A = 1, B = 2 };
    return 7;
}
