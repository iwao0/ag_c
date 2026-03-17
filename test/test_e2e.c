#include "test_common.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum {
  CASE_INT,
  CASE_FLOAT,
  CASE_DOUBLE,
} case_kind_t;

typedef struct {
  const char *category;
  const char *name;
  case_kind_t kind;
  const char *input;
  int expected_i;
  double expected_f;
} test_case_t;

static void build_artifact_paths(const test_case_t *tc, char *dir, char *s_path, char *bin_path, char *drv_path) {
  snprintf(dir, PATH_MAX, "build/e2e/%s", tc->category);
  snprintf(s_path, PATH_MAX, "%s/%s.s", dir, tc->name);
  snprintf(bin_path, PATH_MAX, "%s/%s", dir, tc->name);
  if (drv_path) {
    snprintf(drv_path, PATH_MAX, "%s/%s_driver.c", dir, tc->name);
  }
}

static void build_source_path(const test_case_t *tc, char *src_path) {
  snprintf(src_path, PATH_MAX, "build/e2e/%s/%s.c", tc->category, tc->name);
}

static const test_case_t test_cases[] = {
    {"integer", "zero", CASE_INT, "main() { return 0; }", 0, 0},
    {"integer", "literal", CASE_INT, "main() { return 42; }", 42, 0},

    {"arithmetic", "add_sub", CASE_INT, "main() { return 5+20-4; }", 21, 0},
    {"arithmetic", "spaces", CASE_INT, "main() { return 12 + 34 - 5 ; }", 41, 0},
    {"arithmetic", "mul", CASE_INT, "main() { return 5+6*7; }", 47, 0},
    {"arithmetic", "paren", CASE_INT, "main() { return 5*(9-6); }", 15, 0},
    {"arithmetic", "div", CASE_INT, "main() { return (3+5)/2; }", 4, 0},
    {"arithmetic", "mod", CASE_INT, "main() { return 10%3; }", 1, 0},
    {"arithmetic", "mod_prec", CASE_INT, "main() { return 10+7%4*2; }", 16, 0},
    {"arithmetic", "mod_neg_lhs", CASE_INT, "main() { return (-10%3)==-1; }", 1, 0},
    {"arithmetic", "mod_neg_rhs", CASE_INT, "main() { return (10%-3)==1; }", 1, 0},
    {"arithmetic", "mod_zero_impl_defined", CASE_INT, "main() { return 10%0; }", 10, 0},
    {"arithmetic", "unary_plus", CASE_INT, "main() { return +42; }", 42, 0},
    {"arithmetic", "unary_minus", CASE_INT, "main() { return -7+10; }", 3, 0},
    {"arithmetic", "logical_not_true", CASE_INT, "main() { return !0; }", 1, 0},
    {"arithmetic", "logical_not_false", CASE_INT, "main() { return !5; }", 0, 0},
    {"arithmetic", "bit_not", CASE_INT, "main() { return ~5; }", 250, 0},
    {"arithmetic", "pre_inc", CASE_INT, "main() { a=1; return ++a; }", 2, 0},
    {"arithmetic", "post_inc", CASE_INT, "main() { a=1; b=a++; return a*10+b; }", 21, 0},
    {"arithmetic", "pre_dec", CASE_INT, "main() { a=3; return --a; }", 2, 0},
    {"arithmetic", "post_dec", CASE_INT, "main() { a=3; b=a--; return a*10+b; }", 23, 0},
    {"arithmetic", "add_eq", CASE_INT, "main() { a=5; a+=3; return a; }", 8, 0},
    {"arithmetic", "sub_eq", CASE_INT, "main() { a=5; a-=3; return a; }", 2, 0},
    {"arithmetic", "mul_eq", CASE_INT, "main() { a=5; a*=3; return a; }", 15, 0},
    {"arithmetic", "div_eq", CASE_INT, "main() { a=8; a/=2; return a; }", 4, 0},
    {"arithmetic", "mod_eq", CASE_INT, "main() { a=10; a%=4; return a; }", 2, 0},
    {"arithmetic", "shl_eq", CASE_INT, "main() { a=3; a<<=2; return a; }", 12, 0},
    {"arithmetic", "shr_eq", CASE_INT, "main() { a=32; a>>=3; return a; }", 4, 0},
    {"arithmetic", "and_eq", CASE_INT, "main() { a=14; a&=3; return a; }", 2, 0},
    {"arithmetic", "xor_eq", CASE_INT, "main() { a=14; a^=3; return a; }", 13, 0},
    {"arithmetic", "or_eq", CASE_INT, "main() { a=8; a|=3; return a; }", 11, 0},
    {"arithmetic", "comma_basic", CASE_INT, "main() { a=0; return (a=1, a+2); }", 3, 0},
    {"arithmetic", "comma_chain", CASE_INT, "main() { a=0; b=0; return (a=1, b=2, a+b); }", 3, 0},

    {"comparison", "eq1", CASE_INT, "main() { return 0==0; }", 1, 0},
    {"comparison", "eq2", CASE_INT, "main() { return 42==0; }", 0, 0},
    {"comparison", "neq1", CASE_INT, "main() { return 0!=1; }", 1, 0},
    {"comparison", "neq2", CASE_INT, "main() { return 42!=42; }", 0, 0},
    {"comparison", "lt1", CASE_INT, "main() { return 0<1; }", 1, 0},
    {"comparison", "lt2", CASE_INT, "main() { return 1<1; }", 0, 0},
    {"comparison", "lt3", CASE_INT, "main() { return 2<1; }", 0, 0},
    {"comparison", "le1", CASE_INT, "main() { return 0<=1; }", 1, 0},
    {"comparison", "le2", CASE_INT, "main() { return 1<=1; }", 1, 0},
    {"comparison", "le3", CASE_INT, "main() { return 2<=1; }", 0, 0},
    {"comparison", "gt1", CASE_INT, "main() { return 1>0; }", 1, 0},
    {"comparison", "gt2", CASE_INT, "main() { return 1>1; }", 0, 0},
    {"comparison", "gt3", CASE_INT, "main() { return 1>2; }", 0, 0},
    {"comparison", "ge1", CASE_INT, "main() { return 1>=0; }", 1, 0},
    {"comparison", "ge2", CASE_INT, "main() { return 1>=1; }", 1, 0},
    {"comparison", "ge3", CASE_INT, "main() { return 1>=2; }", 0, 0},
    {"comparison", "log_and", CASE_INT, "main() { return 1&&2; }", 1, 0},
    {"comparison", "log_or", CASE_INT, "main() { return 0||2; }", 1, 0},
    {"comparison", "log_prec", CASE_INT, "main() { return 1||0&&0; }", 1, 0},
    {"comparison", "short_and", CASE_INT, "main() { a=0; b=0; if (a && (b=1)) b=2; return b; }", 0, 0},
    {"comparison", "short_or", CASE_INT, "main() { a=1; b=0; if (a || (b=1)) b=b+2; return b; }", 2, 0},
    {"comparison", "ternary_true", CASE_INT, "main() { return 1 ? 10 : 20; }", 10, 0},
    {"comparison", "ternary_false", CASE_INT, "main() { return 0 ? 10 : 20; }", 20, 0},
    {"comparison", "ternary_nested", CASE_INT, "main() { return 0 ? 1 : 1 ? 2 : 3; }", 2, 0},
    {"local_variables", "basic", CASE_INT, "main() { a=3; return a; }", 3, 0},
    {"local_variables", "expr", CASE_INT, "main() { a=3; b=5*6-8; return a+b/2; }", 14, 0},
    {"local_variables", "sum3", CASE_INT, "main() { a=1; b=2; c=3; return a+b+c; }", 6, 0},
    {"local_variables", "mul2", CASE_INT, "main() { a=5; return a*2; }", 10, 0},
    {"local_variables", "copy", CASE_INT, "main() { a=1; b=a; return b; }", 1, 0},

    {"if_else", "if_true", CASE_INT, "main() { a=3; if (a==3) return a; else return 0; }", 3, 0},
    {"if_else", "if_false", CASE_INT, "main() { a=3; if (a==5) return a; else return 0; }", 0, 0},
    {"if_else", "branch1", CASE_INT, "main() { a=3; if (a==3) return 5; else return 10; }", 5, 0},
    {"if_else", "branch2", CASE_INT, "main() { a=3; if (a!=3) return 5; else return 10; }", 10, 0},
    {"if_else", "literal1", CASE_INT, "main() { if (1) return 2; else return 3; }", 2, 0},
    {"if_else", "literal0", CASE_INT, "main() { if (0) return 2; else return 3; }", 3, 0},
    {"if_else", "fallthrough", CASE_INT, "main() { if (1) return 42; return 0; }", 42, 0},

    {"while", "count", CASE_INT, "main() { a=0; while (a<10) a=a+1; return a; }", 10, 0},
    {"while", "zero", CASE_INT, "main() { a=0; while (0) a=a+1; return a; }", 0, 0},
    {"while", "do_once", CASE_INT, "main() { a=0; do a=a+1; while (0); return a; }", 1, 0},
    {"while", "do_loop", CASE_INT, "main() { a=0; do a=a+1; while (a<5); return a; }", 5, 0},
    {"while", "break", CASE_INT, "main() { a=0; while (1) { a=a+1; break; } return a; }", 1, 0},
    {"while", "continue", CASE_INT, "main() { a=0; b=0; while (a<5) { a=a+1; if (a==3) continue; b=b+a; } return b; }", 12, 0},
    {"while", "for_break_continue", CASE_INT, "main() { i=0; s=0; for (i=0; i<6; i=i+1) { if (i==2) continue; if (i==5) break; s=s+i; } return s; }", 8, 0},
    {"while", "do_continue", CASE_INT, "main() { a=0; b=0; do { a=a+1; if (a<3) continue; b=b+a; } while (a<4); return b; }", 7, 0},

    {"for", "sum10", CASE_INT, "main() { a=0; b=0; for (a=1; a<=10; a=a+1) b=b+a; return b; }", 55, 0},
    {"for", "inc", CASE_INT, "main() { a=0; for (a=0; a<10; a=a+1) a; return a; }", 10, 0},
    {"for", "post_inc_expr", CASE_INT, "main() { a=0; for (a=0; a<5; a++) a; return a; }", 5, 0},

    {"bitwise", "bit_and", CASE_INT, "main() { return 6 & 3; }", 2, 0},
    {"bitwise", "bit_xor", CASE_INT, "main() { return 6 ^ 3; }", 5, 0},
    {"bitwise", "bit_or", CASE_INT, "main() { return 6 | 3; }", 7, 0},
    {"bitwise", "bit_precedence", CASE_INT, "main() { return 1 | 2 ^ 3 & 4; }", 3, 0},
    {"bitwise", "bit_vs_logical_prec", CASE_INT, "main() { return 1 && 2 | 0; }", 1, 0},

    {"shift", "shl", CASE_INT, "main() { return 3 << 2; }", 12, 0},
    {"shift", "shr", CASE_INT, "main() { return 32 >> 3; }", 4, 0},
    {"shift", "shift_precedence", CASE_INT, "main() { return 1 + 2 << 3; }", 24, 0},
    {"shift", "shift_neg_right", CASE_INT, "main() { return (-8 >> 1) == -4; }", 1, 0},
    {"shift", "shift_by_zero", CASE_INT, "main() { return (5 << 0) == 5; }", 1, 0},
    {"shift", "shift_large_bit", CASE_INT, "main() { return (1 << 30) > 0; }", 1, 0},

    {"switch_edge", "match", CASE_INT, "main() { a=2; switch (a) { case 1: return 10; case 2: return 20; default: return 30; } }", 20, 0},
    {"switch_edge", "default", CASE_INT, "main() { a=9; switch (a) { case 1: return 10; case 2: return 20; default: return 30; } }", 30, 0},
    {"switch_edge", "fallthrough", CASE_INT, "main() { a=1; b=0; switch (a) { case 1: b=b+1; case 2: b=b+2; break; default: b=99; } return b; }", 3, 0},
    {"switch_edge", "break_in_switch", CASE_INT, "main() { a=1; b=0; switch (a) { case 1: b=7; break; default: b=9; } return b; }", 7, 0},
    {"switch_edge", "continue_outer_loop", CASE_INT, "main() { i=0; s=0; while (i<4) { i=i+1; switch (i) { case 2: continue; default: s=s+i; } } return s; }", 8, 0},
    {"switch_edge", "goto_forward", CASE_INT, "main() { goto L1; return 0; L1: return 42; }", 42, 0},
    {"switch_edge", "goto_backward_loop", CASE_INT, "main() { i=0; L: i=i+1; if (i<3) goto L; return i; }", 3, 0},

    {"return", "literal", CASE_INT, "main() { return 42; }", 42, 0},
    {"return", "expr", CASE_INT, "main() { return 2+3; }", 5, 0},
    {"return", "var", CASE_INT, "main() { a=10; return a; }", 10, 0},
    {"return", "sum", CASE_INT, "main() { a=1; b=2; return a+b; }", 3, 0},
    {"return", "if", CASE_INT, "main() { if (1) return 1; else return 2; }", 1, 0},
    {"return", "while", CASE_INT, "main() { a=0; while (a<10) a=a+1; return a; }", 10, 0},

    {"block", "stmts", CASE_INT, "main() { { 1; 2; 3; } return 3; }", 3, 0},
    {"block", "sum", CASE_INT, "main() { a=1; b=2; c=3; { a+b+c; } return a+b+c; }", 6, 0},
    {"block", "for", CASE_INT, "main() { a=0; b=0; for (a=1; a<=10; a=a+1) { b=b+a; } return b; }", 55, 0},
    {"block", "while", CASE_INT, "main() { a=0; while (a<10) { a=a+1; } return a; }", 10, 0},
    {"block", "if", CASE_INT, "main() { if (1) { a=2; b=3; return a+b; } else { return 0; } }", 5, 0},

    {"funcall", "noargs", CASE_INT, "fortytwo() { return 42; } main() { return fortytwo(); }", 42, 0},
    {"funcall", "add", CASE_INT, "add(a, b) { return a+b; } main() { return add(3, 4); }", 7, 0},
    {"funcall", "twice", CASE_INT, "twice(a) { return a*2; } main() { return twice(5); }", 10, 0},
    {"funcall", "multi", CASE_INT, "add(a, b) { return a+b; } mul(a, b) { return a*b; } main() { return add(mul(3, 4), mul(3, 3)); }", 21, 0},
    {"funcall", "rec", CASE_INT, "fact(n) { if (n<=1) return 1; return n * fact(n-1); } main() { return fact(5); }", 120, 0},
    {"funcall", "comma_arg", CASE_INT, "f(x,y){return x*10+y;} main(){ return f((1,2),3); }", 23, 0},
    {"funcall", "prototype_decl", CASE_INT, "int add(int a, int b); int add(int a, int b){ return a+b; } int main(){ return add(20,22); }", 42, 0},
    {"funcall", "printf_variadic", CASE_INT, "#include <stdio.h>\nint main() { return printf(\"x=%d\\n\", 42) == 5 ? 0 : 1; }", 0, 0},

    {"multichar_var", "foo", CASE_INT, "main() { foo=3; return foo; }", 3, 0},
    {"multichar_var", "hello", CASE_INT, "main() { hello=2; world=3; return hello+world; }", 5, 0},
    {"multichar_var", "x1x2", CASE_INT, "main() { x1=5; x2=10; return x1+x2; }", 15, 0},
    {"multichar_var", "args", CASE_INT, "add(lhs, rhs) { return lhs+rhs; } main() { return add(3, 7); }", 10, 0},
    {"multichar_var", "loop", CASE_INT, "main() { count=0; for (i=1; i<=3; i=i+1) count=count+i; return count; }", 6, 0},

    {"type_decl", "int_func", CASE_INT, "int main() { return 42; }", 42, 0},
    {"type_decl", "int_var", CASE_INT, "int main() { int x = 3; return x; }", 3, 0},
    {"type_decl", "int_sum", CASE_INT, "int main() { int a = 3; int b = 4; return a+b; }", 7, 0},
    {"type_decl", "int_args", CASE_INT, "int add(int a, int b) { return a+b; } int main() { return add(3, 7); }", 10, 0},
    {"type_decl", "int_init", CASE_INT, "int main() { int x; x = 5; return x; }", 5, 0},
    {"type_decl", "multi_decl_one_init", CASE_INT, "int main() { int a, b=7; return b; }", 7, 0},
    {"type_decl", "multi_decl_two_init", CASE_INT, "int main() { int a=3, b=4; return a+b; }", 7, 0},
    {"type_decl", "for_decl", CASE_INT, "int main() { int sum = 0; int i; for (i=1; i<=10; i=i+1) sum=sum+i; return sum; }", 55, 0},
    {"type_decl", "for_multi_decl_init", CASE_INT, "int main() { int s=0; for (int i=0, j=3; i<j; i=i+1) s=s+i; return s; }", 3, 0},
    {"type_decl", "tag_decl_minimal", CASE_INT, "int main() { struct S; union U; enum E; return 7; }", 7, 0},
    {"type_decl", "tag_decl_ref_ptr", CASE_INT, "int main() { struct S; struct S *p; p=0; return p==0; }", 1, 0},
    {"type_decl", "tag_def_struct", CASE_INT, "int main() { struct S { int x; }; return 7; }", 7, 0},
    {"type_decl", "tag_def_and_ptr_decl", CASE_INT, "int main() { struct S { int x; } *p; p=0; return p==0; }", 1, 0},
    {"type_decl", "tag_def_union_enum", CASE_INT, "int main() { union U { int x; char y; }; enum E { A=1, B=2 }; return 7; }", 7, 0},
    {"type_decl", "enum_const_ref", CASE_INT, "int main() { enum E { A=1, B, C=10 }; return A+B+C; }", 13, 0},
    {"type_decl", "global_tag_before_main", CASE_INT, "struct S { int x; }; int main() { return 7; }", 7, 0},
    {"type_decl", "global_tag_decl_with_var", CASE_INT, "struct S { int x; } *gp; int main() { return 7; }", 7, 0},
    {"type_decl", "global_int_var_decl", CASE_INT, "int g = 1; int main() { return 7; }", 7, 0},
    {"type_decl", "char", CASE_INT, "int main() { char c = 65; return c; }", 65, 0},
    {"type_decl", "void", CASE_INT, "void noop() { return; } int main() { noop(); return 42; }", 42, 0},
    {"type_decl", "short", CASE_INT, "int main() { short s = 10; return s; }", 10, 0},
    {"type_decl", "long", CASE_INT, "long calc(long x) { return x+1; } int main() { return calc(98); }", 99, 0},
    {"type_decl", "short_arr", CASE_INT, "int main() { short arr[3]; arr[0]=10; arr[1]=20; arr[2]=30; return arr[2]; }", 30, 0},
    {"type_decl", "short_sum", CASE_INT, "int main() { short arr[3]; arr[0]=10; arr[1]=20; arr[2]=30; return arr[0]+arr[1]+arr[2]; }", 60, 0},
    {"type_decl", "short_one", CASE_INT, "int main() { short a = 42; return a; }", 42, 0},
    {"type_decl", "unsigned_decl", CASE_INT, "int main() { unsigned u = 42; return u; }", 42, 0},
    {"type_decl", "bool_decl", CASE_INT, "int main() { _Bool b = 1; return b; }", 1, 0},
    {"type_decl", "signed_decl", CASE_INT, "int main() { signed s = -3; return s + 4; }", 1, 0},
    {"type_decl", "char_add_eq", CASE_INT, "int main() { char c = 1; c += 2; return c; }", 3, 0},
    {"type_decl", "short_mul_eq", CASE_INT, "int main() { short s = 10; s *= 3; return s; }", 30, 0},
    {"type_decl", "ptr_deref_add_eq", CASE_INT, "int main() { int x = 5; int *p = &x; *p += 2; return x; }", 7, 0},
    {"type_decl", "sizeof_int", CASE_INT, "int main() { return sizeof(int); }", 4, 0},
    {"type_decl", "sizeof_bool", CASE_INT, "int main() { return sizeof(_Bool); }", 1, 0},
    {"type_decl", "sizeof_int_ptr", CASE_INT, "int main() { return sizeof(int*); }", 8, 0},
    {"type_decl", "sizeof_expr_var", CASE_INT, "int main() { int x = 3; return sizeof(x); }", 4, 0},
    {"type_decl", "cast_int", CASE_INT, "int main() { return (int)42; }", 42, 0},
    {"type_decl", "cast_char_wrap", CASE_INT, "int main() { return (char)300; }", 44, 0},
    {"type_decl", "cast_short_wrap", CASE_INT, "int main() { return (short)(700*100); }", 112, 0},
    {"type_decl", "cast_bool_true", CASE_INT, "int main() { return (_Bool)3; }", 1, 0},
    {"type_decl", "cast_bool_false", CASE_INT, "int main() { return (_Bool)0; }", 0, 0},
    {"type_decl", "cast_unsigned", CASE_INT, "int main() { return (unsigned)42; }", 42, 0},
    {"type_decl", "cast_tag_ptr", CASE_INT, "int main() { struct S { int x; }; struct S *p = 0; return ((struct S*)p)==0; }", 1, 0},
    {"type_decl", "float1", CASE_FLOAT, "float ag_m() { float f = 7; return f; }", 0, 7.0},
    {"type_decl", "float2", CASE_FLOAT, "float ag_m() { float f = 3.14; float g = 4.2; return f + g; }", 0, 7.34},
    {"type_decl", "float3", CASE_FLOAT, "float ag_m() { float f = 5.5; float g = 3.2; return f - g; }", 0, 2.3},
    {"type_decl", "float4", CASE_FLOAT, "float ag_m() { float f = 6.0f; float g = 2.5f; return f * g; }", 0, 15.0},
    {"type_decl", "float5", CASE_FLOAT, "float ag_m() { float f = 10.5F; float g = 3.0F; return f / g; }", 0, 3.5},
    {"type_decl", "double1", CASE_DOUBLE, "double ag_m() { double d = 3.99; return d; }", 0, 3.99},
    {"type_decl", "double2", CASE_DOUBLE, "double ag_m() { double a = 3.1; double b = 4.2; return a + b; }", 0, 7.3},
    {"type_decl", "double3", CASE_DOUBLE, "double ag_m() { double a = 5.0; double b = 3.0; return a * b; }", 0, 15.0},
    {"type_decl", "double4", CASE_DOUBLE, "double ag_m() { double a = 15.0; double b = 3.0; return a / b; }", 0, 5.0},

    {"pointer", "deref", CASE_INT, "int main() { int x = 5; int *p = &x; return *p; }", 5, 0},
    {"pointer", "assign", CASE_INT, "int main() { int x = 5; int *p = &x; *p = 10; return x; }", 10, 0},

    {"array", "idx", CASE_INT, "int main() { int arr[3]; arr[0]=1; arr[1]=2; arr[2]=3; return arr[2]; }", 3, 0},
    {"array", "sum", CASE_INT, "int main() { int arr[3]; arr[0]=1; arr[1]=2; arr[2]=3; return arr[0]+arr[1]+arr[2]; }", 6, 0},
    {"array", "loop", CASE_INT, "int main() { int arr[10]; int i; for(i=0; i<10; i=i+1) arr[i]=i+1; int sum=0; for(i=0; i<10; i=i+1) sum=sum+arr[i]; return sum; }", 55, 0},

    {"string", "deref", CASE_INT, "int main() { char *s = \"AB\"; return *s; }", 65, 0},
    {"string", "index", CASE_INT, "int main() { char *s = \"AB\"; return s[1]; }", 66, 0},
    {"string", "empty", CASE_INT, "int main() { char *s = \"\"; return *s; }", 0, 0},
    {"string", "charlit", CASE_INT, "int main() { return 'A'; }", 65, 0},
    {"string", "newline", CASE_INT, "int main() { return '\\n'; }", 10, 0},
    {"string", "nul", CASE_INT, "int main() { return '\\0'; }", 0, 0},
    {"string", "buf_idx", CASE_INT, "int main() { char buf[3]; buf[0]=1; buf[1]=2; buf[2]=3; return buf[2]; }", 3, 0},
    {"string", "buf_sum", CASE_INT, "int main() { char buf[3]; buf[0]=1; buf[1]=2; buf[2]=3; return buf[0]+buf[1]+buf[2]; }", 6, 0},
    {"string", "char_var", CASE_INT, "int main() { char c = 42; return c; }", 42, 0},
};

static int test_count = 0;
static int pass_count = 0;

static int mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len == 0) return 0;
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
  return 0;
}

static int run_ag_c_to_s(const char *input, const char *s_path) {
  pid_t pid = fork();
  if (pid == 0) {
    freopen(s_path, "w", stdout);
    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    _exit(1);
  }
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
  return 0;
}

static int write_source_file(const char *path, const char *source) {
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  fputs(source, fp);
  fclose(fp);
  return 0;
}

static int build_case(const test_case_t *tc) {
  char dir[PATH_MAX];
  char s_path[PATH_MAX];
  char bin_path[PATH_MAX];
  char drv_path[PATH_MAX];
  char src_path[PATH_MAX];
  char cmd[PATH_MAX * 2];

  build_artifact_paths(tc, dir, s_path, bin_path, drv_path);
  build_source_path(tc, src_path);
  if (mkdir_p(dir) != 0) return -1;
  if (write_source_file(src_path, tc->input) != 0) return -1;

  if (tc->kind == CASE_INT) {
    if (run_ag_c_to_s(src_path, s_path) != 0) return -1;
    snprintf(cmd, sizeof(cmd), "clang -o %s %s 2>&1", bin_path, s_path);
    if (system(cmd) != 0) return -1;
    return 0;
  }

  if (run_ag_c_to_s(src_path, s_path) != 0) return -1;

  FILE *fp = fopen(drv_path, "w");
  if (!fp) return -1;
  if (tc->kind == CASE_DOUBLE) {
    fprintf(fp, "#include <stdio.h>\nextern double ag_m();\nint main() { printf(\"%%.6lf\\n\", ag_m()); return 0; }\n");
  } else {
    fprintf(fp, "#include <stdio.h>\nextern float ag_m();\nint main() { printf(\"%%.6f\\n\", ag_m()); return 0; }\n");
  }
  fclose(fp);

  snprintf(cmd, sizeof(cmd), "clang -o %s %s %s 2>&1", bin_path, s_path, drv_path);
  if (system(cmd) != 0) return -1;
  return 0;
}

static int run_case(const test_case_t *tc, FILE *log) {
  char dir[PATH_MAX];
  char s_path[PATH_MAX];
  char bin_path[PATH_MAX];
  build_artifact_paths(tc, dir, s_path, bin_path, NULL);
  if (tc->kind == CASE_INT) {
    int status = system(bin_path);
    if (status == -1) {
      fprintf(log, "  FAIL: %s could not run\n  input: %s\n  artifacts: s=%s bin=%s\n",
              tc->name, tc->input, s_path, bin_path);
      return -1;
    }
    if (!WIFEXITED(status)) {
      fprintf(log, "  FAIL: %s terminated unexpectedly\n  input: %s\n  artifacts: s=%s bin=%s\n",
              tc->name, tc->input, s_path, bin_path);
      return -1;
    }
    int actual = WEXITSTATUS(status);
    if (actual == tc->expected_i) {
      fprintf(log, "  OK: %s => %d\n", tc->name, actual);
      return 0;
    }
    fprintf(log, "  FAIL: %s expected %d, got %d\n  input: %s\n  artifacts: s=%s bin=%s\n",
            tc->name, tc->expected_i, actual, tc->input, s_path, bin_path);
    return -1;
  }

  char cmd[PATH_MAX * 2];
  snprintf(cmd, sizeof(cmd), "%s", bin_path);
  FILE *out = popen(cmd, "r");
  if (!out) {
    fprintf(log, "  FAIL: %s could not run\n  artifacts: s=%s bin=%s\n", tc->name, s_path, bin_path);
    return -1;
  }
  double actual = 0.0;
  if (tc->kind == CASE_DOUBLE) {
    fscanf(out, "%lf", &actual);
  } else {
    float f = 0.0f;
    fscanf(out, "%f", &f);
    actual = f;
  }
  pclose(out);
  if (actual > tc->expected_f - 0.001 && actual < tc->expected_f + 0.001) {
    fprintf(log, "  OK: %s => %.2f\n", tc->name, actual);
    return 0;
  }
  fprintf(log, "  FAIL: %s expected %.2f, got %.2f\n  input: %s\n  artifacts: s=%s bin=%s\n",
          tc->name, tc->expected_f, actual, tc->input, s_path, bin_path);
  return -1;
}

static int run_category(const char *category) {
  char log_path[PATH_MAX];
  snprintf(log_path, sizeof(log_path), "build/e2e/logs/%s.log", category);
  FILE *log = fopen(log_path, "w");
  if (!log) return -1;
  fprintf(log, "Category: %s\n", category);
  int total = 0;
  int ok = 0;
  for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
    const test_case_t *tc = &test_cases[i];
    if (strcmp(tc->category, category) != 0) continue;
    total++;
    if (run_case(tc, log) == 0) ok++;
  }
  fprintf(log, "Summary: %d/%d passed\n", ok, total);
  fclose(log);
  return (ok == total) ? 0 : 1;
}

static int build_category(const char *category) {
  char log_path[PATH_MAX];
  snprintf(log_path, sizeof(log_path), "build/e2e/logs/%s.build.log", category);
  FILE *log = fopen(log_path, "w");
  if (!log) return -1;
  fprintf(log, "Category: %s\n", category);
  for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
    const test_case_t *tc = &test_cases[i];
    if (strcmp(tc->category, category) != 0) continue;
    if (build_case(tc) != 0) {
      char dir[PATH_MAX], s_path[PATH_MAX], bin_path[PATH_MAX], drv_path[PATH_MAX];
      build_artifact_paths(tc, dir, s_path, bin_path, drv_path);
      fprintf(log, "  FAIL: build %s\n  input: %s\n  artifacts: s=%s bin=%s driver=%s\n",
              tc->name, tc->input, s_path, bin_path, drv_path);
      fclose(log);
      return 1;
    }
  }
  fprintf(log, "Summary: build OK\n");
  fclose(log);
  return 0;
}

int main() {
  printf("Running E2E tests...\n");
  fflush(stdout);

  if (mkdir_p("build/e2e/logs") != 0) {
    fprintf(stderr, "Failed to create log directory\n");
    return 1;
  }

  size_t max_cases = sizeof(test_cases) / sizeof(test_cases[0]);
  const char **categories = calloc(max_cases, sizeof(const char *));
  pid_t *build_pids = calloc(max_cases, sizeof(pid_t));
  pid_t *pids = calloc(max_cases, sizeof(pid_t));
  if (!categories || !build_pids || !pids) {
    fprintf(stderr, "Failed to allocate category buffers\n");
    free(categories);
    free(build_pids);
    free(pids);
    return 1;
  }

  size_t ncat = 0;
  for (size_t i = 0; i < max_cases; i++) {
    const char *cat = test_cases[i].category;
    bool exists = false;
    for (size_t j = 0; j < ncat; j++) {
      if (strcmp(categories[j], cat) == 0) { exists = true; break; }
    }
    if (!exists) categories[ncat++] = cat;
  }

  for (size_t i = 0; i < ncat; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      int rc = build_category(categories[i]);
      _exit(rc);
    }
    build_pids[i] = pid;
  }

  for (size_t i = 0; i < ncat; i++) {
    int status;
    waitpid(build_pids[i], &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fprintf(stderr, "Build failed: %s (see build/e2e/logs/%s.build.log)\n", categories[i], categories[i]);
      free(categories);
      free(build_pids);
      free(pids);
      return 1;
    }
  }

  for (size_t i = 0; i < ncat; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      int rc = run_category(categories[i]);
      _exit(rc);
    }
    pids[i] = pid;
  }

  int failed = 0;
  for (size_t i = 0; i < ncat; i++) {
    int status;
    waitpid(pids[i], &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      failed = 1;
      fprintf(stderr, "Category failed: %s (see build/e2e/logs/%s.log)\n", categories[i], categories[i]);
    }
  }

  test_count = (int)(sizeof(test_cases) / sizeof(test_cases[0]));
  pass_count = failed ? 0 : test_count;

  free(categories);
  free(build_pids);
  free(pids);
  if (failed) return 1;
  printf("OK: All %d E2E tests passed! (%d/%d)\n", test_count, pass_count, test_count);
  return 0;
}
