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
#include <assert.h>
struct Ops {
    double (*d)(double);
    float  (*f)(float);
};
struct Holder {
    struct Ops ops[2];
    struct Ops *p;
};
double dbl(double x){ return x * 2.0; }
float  inc(float x){ return x + 1.0f; }
double half(double x){ return x / 2.0; }

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
    assert(a == 42);
    assert(b == 41);
    assert(c == 1);

    // nested struct / struct-array member 経由
    struct Holder h;
    h.ops[0].d = dbl;
    h.ops[0].f = inc;
    h.ops[1].d = half;
    h.ops[1].f = inc;
    h.p = &o;
    assert((int)h.ops[0].d(21.0) == 42);
    assert((int)h.ops[1].d(84.0) == 42);
    assert((int)h.ops[0].f(41.0f) == 42);
    assert((int)h.p->d(21.0) == 42);
    assert((int)h.p->f(41.0f) == 42);
    return 0;
}
