// カンマ式の値を union コピー初期化に
// 期待: exit=9
int main(void) {
    union U { int x; char y; };
    union U v = {7};
    union U u = (v.x = 9, v);
    return u.x;
}
