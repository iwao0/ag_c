// 要素数 1 の括弧内配列グローバル `T (*g[1])(...)` / `T (*g[1])` が crash していたバグ。
// `g[0](x)` / `*g[0]` が SIGSEGV / 誤値 (int/double 問わず)。
// 原因: 括弧内配列 `(*g[1])` は paren_array_mul=1 となり、parse_toplevel_array_suffixes の
//      is_array = (base_mul > 1) が要素数 1 を取りこぼし、g が「配列」ではなくスカラ
//      funcptr / ポインタとして誤登録され、subscript が実体をロードせず crash していた。
// 修正: 括弧内に配列サフィックスがあれば (g_toplevel_decl_paren_array_present) 要素数に
//      よらず配列として登録する。pointer-to-array (trailing `[N]`) とは別経路。
// 修正前: SIGSEGV / 誤値
// 期待: exit=42
#include <assert.h>
int  iadd(int x){ return x + 1; }
double dadd(double x){ return x + 1.5; }
int    va = 40, vb = 9;

int    (*gi[1])(int)      = { iadd };   // size-1 funcptr array
double (*gd[1])(double)   = { dadd };   // size-1 funcptr array (double return)
int    *gp[1]             = { &va };    // size-1 pointer array (normal suffix)
int    (*gq[1])           = { &vb };    // size-1 pointer array (redundant parens)

int main(void){
    int a = gi[0](41);              // 42
    int b = (int)(gd[0](40.5));     // 42.0 -> 42
    int c = *gp[0] + *gq[0];        // 40 + 9 = 49
    assert(a == 42);
    assert(b == 42);
    assert(c == 49);
    return 0;
}
