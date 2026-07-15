// グローバル変数の多段ポインタ `int **gp;` で `**gp` が SIGSEGV していた。第1 deref
// `*gp` が int* (8B) でなく int (4B, ldrsw) としてロードされ、続く deref が壊れた値を
// アドレスとして deref していたのが原因。直書き・pointer typedef 経由の両方で発生。
//   原因: register_toplevel_global_decl がポインタの deref_size を常に要素サイズ (=4) に
//   していて、global_var_t がポインタ段数を持たなかった。ローカル `int **lp` は lvar の
//   pointer_qual_levels=2 / base_deref_size で `*lp` を 8B ポインタロードにしていた。
//   修正: global_var_t に pointer_qual_levels を追加し (宣言子 `*` 数 + 基底ポインタ
//   typedef 段数)、try_build_global_var_node が pql>=2 のとき参照ノードの deref_size=8 /
//   base_deref_size=要素サイズ / pql を立て、build_unary_deref_node の多段 deref 分岐に
//   乗せる (ローカルと同一表現)。
#include <assert.h>

typedef int **PP;
typedef double **DPP;

int    x = 9;
int   *xp = &x;
int  **gp = &xp;        // 直書き 2 段
PP     tp = &xp;        // typedef 経由 2 段

int    y = 7;
int   *yp = &y;
int  **yb = &yp;
int ***g3 = &yb;        // 3 段

char   c = 65;
char  *cp = &c;
char **gcp = &cp;       // char 2 段

struct S { int a, b; };
struct S  obj = {3, 4};
struct S *sp = &obj;
struct S **gsp = &sp;   // struct 2 段

int    arr[3] = {10, 20, 30};
int   *ap = arr;
int  **gap = &ap;       // subscript 用

double    dd = 2.5;
double   *ddp = &dd;
double  **gdp = &ddp;   // fp pointee の 2 段
DPP       tdp = &ddp;   // typedef 経由 fp pointee

int local_double_multilevel(void) {
    double d = 3.5;
    double *p = &d;
    double **pp = &p;
    DPP tpp = &p;
    return (**pp == 3.5 && **tpp == 3.5);
}

int main(void) {
    assert(sizeof(gp) == sizeof(void*));
    assert(**gp == 9);          // 直書き 2 段 deref
    assert(**tp == 9);          // typedef 経由
    assert(***g3 == 7);         // 3 段
    assert(**gcp == 65);        // char
    assert((**gsp).a == 3 && (*gsp)->b == 4);  // struct
    assert((*gap)[1] == 20);    // 第1 deref 後に subscript

    int z = 99; int *zp = &z;
    *gp = zp;                   // 中間ポインタへの代入
    assert(**gp == 99);

    assert(**gdp == 2.5);
    assert(**tdp == 2.5);
    assert(local_double_multilevel());

    return 0;
}
