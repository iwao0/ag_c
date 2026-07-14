// 関数が関数ポインタを返し、pointer-to-function 経由で呼び出す (c-testsuite 00124)。
// `int (* (*p)(int,int))(int,int)` の declarator 上 `*` が 2 つあるため pql=2 と誤登録され、
// `(*p)(a,b)` が [p] のあと [addr] を二重ロードして SIGBUS していた。
// 戻り funcptr 呼び出し `(*(*p)(a,b))(c,d)` も `*call` で同様に二重ロード。
// 現在は pointer(function(...)) の再帰型から各段階を辿り、戻り関数ポインタと
// `(*call())(args)` の callable decay を解決する。
// 期待: exit=0
int f2(int c, int b) { return c - b; }
int (*f1(int a, int b))(int c, int b) {
    if (a != b) return f2;
    return 0;
}
int main(void) {
    int (* (*p)(int a, int b))(int c, int d) = f1;
    return (*(*p)(0, 2))(2, 2);
}
