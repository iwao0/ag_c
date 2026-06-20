// ポインタ typedef を基底にしたグローバル変数 `typedef int *PI; PI gp;` が、ポインタで
// なく int スカラとして登録されていた (sizeof(gp)=4、subscript `gp[i]` で E3064)。
//   原因: parse_toplevel_decl_after_type のオブジェクト経路が base_is_ptr を 0 固定で
//   宣言子へ渡し、typedef 基底のポインタ性 (g_toplevel_decl_base_is_ptr) を捨てていた。
//   typedef 経路は渡していたのに、オブジェクト経路だけ漏れていた。0 でなく
//   g_toplevel_decl_base_is_ptr を渡すよう修正 (直書き `int *gp` は宣言子の `*` で
//   is_ptr が立つので影響なし)。
//   併せて (1) double ポインタ typedef の pointee fp_kind 伝播 (実効段数で判定)、
//   (2) pointer-to-array typedef グローバル `typedef int (*PA)[3]; PA gp;` の outer_stride
//   設定 (typedef に記録したポインティ dims から) も対応。
// 既知の制約: 多段ポインタ typedef `typedef int **PP;` は段数を保存しないためローカル/
// グローバルとも未対応 (別件)。
#include <assert.h>

typedef int      *PI;
typedef char     *PC;
typedef double   *PD;
typedef long     *PL;
typedef unsigned *PU;

struct S { int a, b; };
typedef struct S *PS;

typedef int (*PA)[3];        // pointer-to-array (1 次元 pointee)
typedef int (*PB)[2][3];     // pointer-to-array (多次元 pointee)

int      ia[3] = {7, 8, 9};
char     ca[3] = {1, 2, 3};
double   da[3] = {1.5, 2.5, 3.0};
long     la[3] = {100, 200, 300};
unsigned ua[3] = {5, 6, 7};
struct S obj   = {11, 22};
int      m[2][3]       = {{1, 2, 3}, {4, 5, 6}};
int      c[2][2][3]    = {{{1, 2, 3}, {4, 5, 6}}, {{7, 8, 9}, {10, 11, 12}}};

PI pi = ia;
PC pc = ca;
PD pd = da;
PL pl = la;
PU pu = ua;
PS ps = &obj;
PA pa = m;
PB pb = c;

int main(void) {
    assert(sizeof(pi) == 8);            // ポインタとして登録されること
    assert(pi[0] == 7 && pi[2] == 9);
    assert(pc[0] == 1 && pc[2] == 3);
    assert(pd[1] == 2.5 && *pd == 1.5); // fp pointee の load
    assert(pl[0] == 100 && pl[2] == 300);
    assert(pu[1] == 6 && pu[2] == 7);
    assert(ps->a == 11 && ps->b == 22); // struct ポインタ typedef
    // pointer-to-array typedef: 行送り (outer_stride) が効くこと
    assert(pa[1][0] == 4 && (*(pa + 1))[1] == 5);
    assert(pb[1][0][2] == 9 && pb[0][1][1] == 5);
    return 0;
}
