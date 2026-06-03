// struct の配列メンバを別配列でコピー初期化 (ag_c 拡張)
// a={5,6}, z=7 → 18
// 期待: exit=18
int main(void) {
    int src[2] = {5, 6};
    struct S { int a[2]; int z; };
    struct S s = {src, 7};
    return s.a[0] + s.a[1] + s.z;
}
