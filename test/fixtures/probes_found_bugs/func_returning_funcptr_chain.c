// c-testsuite 00089: 関数が関数ポインタを返し、連鎖呼び出し `go()()->zerofunc()`。
// 修正前: 2 段目 funcall の戻り tag が伝播せず `->zerofunc` が E3005。
int zero(void) { return 0; }
struct S { int (*zerofunc)(void); } s = { &zero };
struct S *anon(void) { return &s; }
typedef struct S * (*fty)(void);
fty go(void) { return &anon; }
int main(void) { return go()()->zerofunc(); }
