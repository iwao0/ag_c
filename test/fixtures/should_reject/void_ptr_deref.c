// void * の deref は不正 (型が決まらない)。明示キャストが必要。
// 期待: ag_c はエラー
int main(void) { int x = 5; void *p = &x; return *p; }
