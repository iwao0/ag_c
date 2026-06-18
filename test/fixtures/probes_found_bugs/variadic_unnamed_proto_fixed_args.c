// 可変長関数のプロトタイプで固定引数が無名 (`int snprintf(char*, unsigned long,
// const char*, ...)` のように引数名を省く一般的な書き方) のとき、固定引数数が 0 と
// 誤算され、可変長呼び出し ABI で固定引数 (format 等) までスタックに積まれて x0 が未設定に
// なり crash していたバグ (Apple ARM64)。`int printf(const char*, ...); printf(...)` も同様。
// 原因: parse_param_decl が無名仮引数を nargs に数えず、psx_ctx_set_function_variadic に
//      固定引数数 0 が渡っていた。caller codegen が「名前付き引数 0 個」として全引数を
//      stack に積んでいた。
// 修正: 通常 (非入れ子) の仮引数リストでは無名でも固定引数として nargs に数える
//      (入れ子宣言子 `int (*(*f(void))(int))[3]` の内側 (int) は f の引数でないので除外)。
// 修正前: SIGSEGV / 値破損
// 期待: exit=42
// 同名定義がある場合は定義 (名前付き) が nargs を上書きするため、本バグは「定義なしの
// 外部可変長関数 + 無名プロトタイプ」で顕在化する。ここでは libc の snprintf を使う。
int snprintf(char *, unsigned long, const char *, ...);   // 固定引数 3 個すべて無名
int main(void){
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d-%d", 12, 30);   // n=5, buf="12-30"
    int ok = (n == 5) && buf[0]=='1' && buf[1]=='2' && buf[2]=='-' && buf[3]=='3' && buf[4]=='0';
    return ok ? 42 : 0;
}
