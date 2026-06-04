// ビットフィールドのアドレス取得は不正 (C11 6.5.3.2p1)。
// 期待: ag_c はアドレス取得エラー
struct S { int f:4; };
int main(void) { struct S s; int *p = &s.f; return 0; }
