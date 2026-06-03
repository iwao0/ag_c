// union ポインタの -> で配列メンバアクセス
// 期待: exit=2 (a[1])
int main(void) {
    union U { int a[2]; int z; };
    union U u = {1, 2};
    return ((union U*)&u)->a[1];
}
