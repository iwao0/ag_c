// double/float の配列スタイル仮引数 `double a[n]` / `double a[]` が壊れていたバグ。
// (1) 配列宣言子なのに param に fp_kind が付き、引数が d レジスタ (fp 値) で受け取られて
//     いた。配列はポインタへ adjust され整数レジスタ渡しなので ABI が壊れ値が化けた。
// (2) register_vla_array_param が pointee_fp_kind を伝播せず、要素サイズ==8 の double で
//     lvar_is_pointer 判定に漏れ subscript が E3064 になっていた。
// 修正: 配列宣言子仮引数には fp_kind を付けない (整数レジスタ受け) + 1D fp 要素配列の
//       pointee_fp_kind を伝播 (ポインタ認識 & fp load/store)。
// 修正前: E3064 または値破損
// 期待: exit=42
#include <assert.h>
double dot(int n, double a[n], double b[n]) {   // 読み出し (VLA 配列引数)
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
void addk(int n, float a[], float k) {          // 書き込み (`[]` 形式)
    for (int i = 0; i < n; i++) a[i] += k;
}
int main(void) {
    double x[3] = {1.0, 2.0, 3.0};
    double y[3] = {4.0, 5.0, 6.0};
    int d = (int)dot(3, x, y);                  // 4+10+18 = 32
    float f[2] = {1.5f, 2.5f};
    addk(2, f, 0.5f);                           // 2.0, 3.0
    int s = (int)(f[0] + f[1] + 5);             // 2+3+5 = 10
    assert(d == 32); assert(s == 10); return 0;                               // 32 + 10 = 42
}
