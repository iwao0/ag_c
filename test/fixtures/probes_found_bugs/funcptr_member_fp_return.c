// struct メンバの関数ポインタ `struct{ double (*f)(double); }` を通した間接呼び出しの
// float/double 戻り値が x0 で読まれて壊れていたバグ (`s.f(x)` / `sp->f(x)`)。
// 原因:
//  (1) struct_layout が funcptr メンバ (head.is_ptr) の戻り型 fp_kind を保存していなかった
//      (fp_kind は !is_ptr メンバのみ保存していた)。
//  (2) funcall ノードに戻り型 fp_kind が載らず ir_builder が戻り値を整数 (x0) と判定。
// 修正: funcptr メンバの戻り fp_kind を member fp_kind に保存し、build_member_deref_node が
//      ポインタメンバの fp_kind を pointee_fp_kind として deref に伝播。parse_call_postfix は
//      callee の pointee_fp_kind を funcall に載せて戻り値を d0 で読む。
// 修正前: 戻り値破損 (x0 を読む)
// 期待: exit=42
struct Ops {
    double (*d)(double);
    float  (*f)(float);
};
double dbl(double x){ return x * 2.0; }
float  inc(float x){ return x + 1.0f; }

int main(void){
    struct Ops o;
    o.d = dbl;
    o.f = inc;

    // `.` 経由 (struct 値)
    double r1 = o.d(21.0);          // 42.0
    float  r2 = o.f(40.0f);         // 41.0f

    // `->` 経由 (struct ポインタ)
    struct Ops *p = &o;
    double r3 = p->d(0.5);          // 1.0

    int a = (int)r1;                // 42
    int b = (int)r2;                // 41
    int c = (int)r3;                // 1
    return (a == 42 && b == 41 && c == 1) ? 42 : 0;
}
