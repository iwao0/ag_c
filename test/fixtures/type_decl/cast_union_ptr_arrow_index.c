// union ポインタの -> で配列 0 番要素
// 期待: exit=1
int main(void) {
    union U { int a[2]; int z; };
    union U u = {1, 2};
    return ((union U*)&u)->a[0];
}
