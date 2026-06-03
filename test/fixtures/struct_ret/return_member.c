// 戻り値の struct のメンバ参照を上位で行う
// 期待: exit=42 (35+7)
struct Pair { int a; int b; };
struct Pair swap(int a, int b) {
    struct Pair p = {b, a};
    return p;
}
int main(void) {
    struct Pair r = swap(7, 35);
    return r.a + r.b;
}
