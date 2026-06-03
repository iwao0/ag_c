// union の波括弧初期化 (先頭メンバ)
// 期待: exit=7
int main(void) {
    union U { int x; char y; };
    union U u = {7};
    return u.x;
}
