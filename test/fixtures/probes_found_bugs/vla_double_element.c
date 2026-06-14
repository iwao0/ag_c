// double/float の VLA (`double a[n]`) の要素アクセスが整数 load/store になり、
// 小数部が失われるバグ。VLA lvar 登録経路は continue で fp_kind 設定をスキップして
// おり、要素型の浮動小数情報が subscript に伝播していなかった。
// 修正: VLA 記述子はポインタ(整数)のまま、要素型を pointee_fp_kind に設定する。
// 修正前: a[1]=2.5 が 2.0 として読まれる等
// 期待: exit=42
int main(void) {
    int n = 3;
    double a[n];
    a[0] = 1.5; a[1] = 2.5; a[2] = 3.0;   // 計 7.0
    float b[n];
    b[0] = 0.5f; b[1] = 1.5f;             // 計 2.0
    double sum = a[0] + a[1] + a[2] + b[0] + b[1];   // 9.0
    return ((int)(sum * 2) == 18 && (int)(a[1] * 10) == 25) ? 42 : 0;
}
