// double/float ポインタ仮引数 `double *a` の deref/subscript が壊れていたバグ。
// 原因:
//  (1) lvar_is_pointer が size>elem_size でポインタ判定するため、size==elem_size==8 の
//      `double *a` がポインタと認識されず subscript が E3064。
//  (2) ポインタ仮引数登録で pointee_fp_kind を伝播しておらず、`*a`/`a[i]` が整数 load +
//      scvtf になって値が壊れていた。
// 修正: スカラポインタ仮引数に pointee_fp_kind を設定し、lvar_is_pointer / 単項 deref /
//      pointee_fp_kind(ND_ADD) で fp ポインタを認識・伝播する。
// 修正前: E3064 または値破損
// 期待: exit=42
double dot(double *a, double *b, int n) {     // 読み出し
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
void addk(float *a, int n, float k) {          // 書き込み
    for (int i = 0; i < n; i++) a[i] += k;
}
int main(void) {
    double x[3] = {1.0, 2.0, 3.0};
    double y[3] = {4.0, 5.0, 6.0};
    int d = (int)dot(x, y, 3);                 // 4+10+18 = 32
    float f[2] = {1.5f, 2.5f};
    addk(f, 2, 0.5f);                          // 2.0, 3.0
    int s = (int)(*f + *(f + 1) + 5);          // 2+3+5 = 10
    return d + s;                              // 32 + 10 = 42
}
