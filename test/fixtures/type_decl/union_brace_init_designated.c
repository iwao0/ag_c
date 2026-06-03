// union の指定初期化子 (.x=7)
// 期待: exit=7
int main(void) {
    union U { int x; char y; };
    union U u = {.x = 7};
    return u.x;
}
