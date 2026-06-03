// union ポインタの -> 経由で後置インクリメント
// a[0]: 1→2, a[1]: 2 → 2+2 = 4
// 期待: exit=4
int main(void) {
    union U { int a[2]; int z; };
    union U u = {1, 2};
    ((union U*)&u)->a[0]++;
    return u.a[0] + u.a[1];
}
