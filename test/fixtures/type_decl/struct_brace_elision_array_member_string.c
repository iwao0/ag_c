// struct の char 配列メンバを文字列で初期化
// "ab" → a[0]='a'=97, a[1]='b'=98, a[2]=0, z=7 → 202
// 期待: exit=202
int main(void) {
    struct S { char a[4]; int z; };
    struct S s = {"ab", 7};
    return s.a[0] + s.a[1] + s.a[2] + s.z;
}
