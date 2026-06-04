// struct を int に代入は不正 (incompatible types)。
// 期待: ag_c はエラー
struct S { int x; };
int main(void) { struct S s; int x = s; return x; }
