// union のメンバ書き込みと読み出し
// 期待: exit=7
int main(void) {
    union U { int x; char y; };
    union U u;
    u.x = 7;
    return u.x;
}
