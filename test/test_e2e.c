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

typedef struct {
  const char *name;
  const char *input;
  const char *expected_diag;
} compile_fail_case_t;

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
    {"switch_edge", "case_const_expr", CASE_INT, "main() { a=3; switch (a) { case 1+2: return 33; default: return 0; } }", 33, 0},
    {"switch_edge", "case_enum_const_expr", CASE_INT, "main() { enum E { A=2 }; a=4; switch (a) { case A*2: return 44; default: return 0; } }", 44, 0},
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
    {"funcall", "tail_rec", CASE_INT, "int sum(int n, int acc) { if (n <= 0) return acc; return sum(n-1, acc+n); } int main() { return sum(10, 0); }", 55, 0},
    {"funcall", "comma_arg", CASE_INT, "f(x,y){return x*10+y;} main(){ return f((1,2),3); }", 23, 0},
    {"funcall", "prototype_decl", CASE_INT, "int add(int a, int b); int add(int a, int b){ return a+b; } int main(){ return add(20,22); }", 42, 0},
    {"funcall", "param_funcptr_decl", CASE_INT, "int apply(int (*fp)(int), int x) { return x; } int main(){ return apply(0,7); }", 7, 0},
    {"funcall", "param_array_decl", CASE_INT, "int f(int a[], int n) { return n; } int main(){ return f(0,5); }", 5, 0},
    {"funcall", "param_array_static_restrict", CASE_INT, "int f(int a[static 3], int b[restrict static 2]) { return 7; } int main(){ return f(0,0); }", 7, 0},
    {"funcall", "funcptr_value_assign_call", CASE_INT, "int inc(int x){ return x+1; } int main(){ int (*fp)(int); fp=inc; return fp(41); }", 42, 0},
    {"funcall", "printf_variadic", CASE_INT, "#include <stdio.h>\nint main() { return printf(\"x=%d\\n\", 42) == 5 ? 0 : 1; }", 0, 0},
    {"funcall", "variadic_proto", CASE_INT, "int log(const char *fmt, ...); int main() { return 7; }", 7, 0},
    {"funcall", "variadic_def", CASE_INT, "int pick(...) { return 9; } int main() { return pick(); }", 9, 0},

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
    {"type_decl", "enum_const_expr", CASE_INT, "int main() { enum E { A=1, B=A+2, C=(B*2)-1 }; return C; }", 5, 0},
    {"type_decl", "enum_const_expr_cond", CASE_INT, "int main() { enum E { A=1, B=(A<2), C=(A==1)&&(B||0), D=C?7:9 }; return D; }", 7, 0},
    {"type_decl", "enum_const_expr_bitwise", CASE_INT, "int main() { enum E { A=1, B=~A, C=(A<<3)|2, D=(C&10)^1 }; return D; }", 11, 0},
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
    {"type_decl", "ptr_ptr_deref", CASE_INT, "int main() { int x=42; int *p=&x; int **pp=&p; return **pp; }", 42, 0},
    {"type_decl", "sizeof_int", CASE_INT, "int main() { return sizeof(int); }", 4, 0},
    {"type_decl", "sizeof_bool", CASE_INT, "int main() { return sizeof(_Bool); }", 1, 0},
    {"type_decl", "sizeof_int_ptr", CASE_INT, "int main() { return sizeof(int*); }", 8, 0},
    {"type_decl", "sizeof_funcptr_type", CASE_INT, "int main() { return sizeof(int (*)(int)); }", 8, 0},
    {"type_decl", "alignof_int", CASE_INT, "int main() { return _Alignof(int); }", 4, 0},
    {"type_decl", "alignof_ptr", CASE_INT, "int main() { return _Alignof(int*); }", 8, 0},
    {"type_decl", "sizeof_expr_var", CASE_INT, "int main() { int x = 3; return sizeof(x); }", 4, 0},
    {"type_decl", "sizeof_struct_type", CASE_INT, "int main() { struct S { int x; }; return sizeof(struct S); }", 4, 0},
    {"type_decl", "alignof_struct_type", CASE_INT, "int main() { struct S { int x; }; return _Alignof(struct S); }", 4, 0},
    {"type_decl", "cast_int", CASE_INT, "int main() { return (int)42; }", 42, 0},
    {"type_decl", "cast_char_wrap", CASE_INT, "int main() { return (char)300; }", 44, 0},
    {"type_decl", "cast_short_wrap", CASE_INT, "int main() { return (short)(700*100); }", 112, 0},
    {"type_decl", "cast_bool_true", CASE_INT, "int main() { return (_Bool)3; }", 1, 0},
    {"type_decl", "cast_bool_false", CASE_INT, "int main() { return (_Bool)0; }", 0, 0},
    {"type_decl", "cast_unsigned", CASE_INT, "int main() { return (unsigned)42; }", 42, 0},
    {"type_decl", "cast_enum", CASE_INT, "int main() { enum E { A=1 }; return (enum E)42; }", 42, 0},
    {"type_decl", "cast_tag_ptr", CASE_INT, "int main() { struct S { int x; }; struct S *p = 0; return ((struct S*)p)==0; }", 1, 0},
    {"type_decl", "cast_struct_from_scalar", CASE_INT, "int main() { struct S { int x; int y; }; return ((struct S)7).x; }", 7, 0},
    {"type_decl", "cast_struct_from_pointer_postfix", CASE_INT, "int main() { struct S { int *p; int q; }; int x=3; return *((struct S)&x).p; }", 3, 0},
    {"type_decl", "cast_struct_same_type", CASE_INT, "int main() { struct S { int x; }; struct S s=(struct S)(struct S){7}; return s.x; }", 7, 0},
    {"type_decl", "cast_struct_diff_tag_same_size", CASE_INT, "int main() { struct A { int x; }; struct B { int x; }; struct A a={7}; struct B b=(struct B)a; return b.x; }", 7, 0},
    {"type_decl", "cast_union_same_type", CASE_INT, "int main() { union U { int x; char y; }; union U u=(union U)(union U){.x=9}; return u.x; }", 9, 0},
    {"type_decl", "cast_union_diff_tag_same_size", CASE_INT, "int main() { union A { int x; }; union B { int x; }; union A a={.x=9}; union B b=(union B)a; return b.x; }", 9, 0},
    {"type_decl", "cast_union_from_scalar", CASE_INT, "int main() { union U { int x; char y; }; return ((union U)7).x; }", 7, 0},
    {"type_decl", "cast_union_from_pointer_postfix", CASE_INT, "int main() { union U { int *p; int q; }; int x=3; return ((union U)&x).p==&x; }", 1, 0},
    {"type_decl", "cast_union_ptr_arrow_chain", CASE_INT, "int main() { union U { int a[2]; int z; }; union U u={1,2}; return ((union U*)&u)->a[1]; }", 2, 0},
    {"type_decl", "cast_union_ptr_arrow_index", CASE_INT, "int main() { union U { int a[2]; int z; }; union U u={1,2}; return ((union U*)&u)->a[0]; }", 1, 0},
    {"type_decl", "cast_union_ptr_arrow_post_inc", CASE_INT, "int main() { union U { int a[2]; int z; }; union U u={1,2}; ((union U*)&u)->a[0]++; return u.a[0]+u.a[1]; }", 4, 0},
    {"type_decl", "cast_union_ptr_arrow_post_dec", CASE_INT, "int main() { union U { int a[2]; int z; }; union U u={1,2}; ((union U*)&u)->a[1]--; return u.a[0]+u.a[1]; }", 2, 0},
    {"type_decl", "cast_atomic_int", CASE_INT, "int main() { return (_Atomic(int))42; }", 42, 0},
    {"type_decl", "member_dot", CASE_INT, "int main() { struct S { int a; int b; }; struct S s; s.a=2; s.b=5; return s.a+s.b; }", 7, 0},
    {"type_decl", "member_arrow", CASE_INT, "int main() { struct S { int a; int b; }; struct S s; struct S *p=&s; p->a=3; p->b=4; return p->a+p->b; }", 7, 0},
    {"type_decl", "member_union", CASE_INT, "int main() { union U { int x; char y; }; union U u; u.x=7; return u.x; }", 7, 0},
    {"type_decl", "union_brace_init_value", CASE_INT, "int main() { union U { int x; char y; }; union U u={7}; return u.x; }", 7, 0},
    {"type_decl", "union_brace_init_designated", CASE_INT, "int main() { union U { int x; char y; }; union U u={.x=7}; return u.x; }", 7, 0},
    {"type_decl", "union_brace_init_multi_designated", CASE_INT, "int main() { union U { int x; char y; }; union U u={.x=7,.y=2}; return u.y; }", 2, 0},
    {"type_decl", "union_array_member_nonbrace_init_values", CASE_INT, "int main() { union U { int a[2]; int z; }; union U u={1,2}; return u.a[0]+u.a[1]; }", 3, 0},
    {"type_decl", "struct_bitfield_decl", CASE_INT, "int main() { struct S { int x:3; int y; }; return 7; }", 7, 0},
    {"type_decl", "struct_anonymous_struct_member", CASE_INT, "int main() { struct S { struct { int x; }; int y; }; return 7; }", 7, 0},
    {"type_decl", "struct_anonymous_union_member", CASE_INT, "int main() { struct S { union { int x; char c; }; int y; }; return 7; }", 7, 0},
    {"type_decl", "struct_brace_init_parse_only", CASE_INT, "int main() { struct S { int x; int y; }; struct S s={1,2}; return 7; }", 7, 0},
    {"type_decl", "struct_brace_init_values", CASE_INT, "int main() { struct S { int x; int y; }; struct S s={1,2}; return s.x+s.y; }", 3, 0},
    {"type_decl", "struct_brace_init_designated", CASE_INT, "int main() { struct S { int x; int y; }; struct S s={.y=2,.x=1}; return s.x+s.y; }", 3, 0},
    {"type_decl", "struct_brace_elision_array_member", CASE_INT, "int main() { struct S { int a[2]; int z; }; struct S s={1,2,3}; return s.z; }", 3, 0},
    {"type_decl", "struct_brace_elision_array_member_copy", CASE_INT, "int main() { int src[2]={5,6}; struct S { int a[2]; int z; }; struct S s={src,7}; return s.a[0]+s.a[1]+s.z; }", 18, 0},
    {"type_decl", "struct_brace_elision_array_member_string", CASE_INT, "int main() { struct S { char a[4]; int z; }; struct S s={\"ab\",7}; return s.a[0]+s.a[1]+s.a[2]+s.z; }", 202, 0},
    {"type_decl", "struct_nested_desig_single", CASE_INT, "int main() { struct S { int a[2]; int z; }; struct S s={.a[1]=3,.z=9}; return s.a[1]; }", 3, 0},
    {"type_decl", "struct_nested_desig_multi", CASE_INT, "int main() { struct S { int a[3]; }; struct S s={.a[0]=7,.a[2]=5}; return s.a[0]+s.a[2]; }", 12, 0},
    {"type_decl", "union_nested_desig", CASE_INT, "int main() { union U { int a[2]; int z; }; union U u={.a[1]=3}; return u.a[1]; }", 3, 0},
    {"type_decl", "struct_single_expr_copy_comma", CASE_INT, "int main() { struct S { int x; int y; }; struct S t={4,5}; struct S s=(t.y=9,t); return s.x+s.y; }", 13, 0},
    {"type_decl", "struct_single_expr_copy_ternary", CASE_INT, "int main() { struct S { int x; int y; }; struct S a={1,2}; struct S b={3,4}; struct S s=(0?a:b); return s.x+s.y; }", 7, 0},
    {"type_decl", "union_single_expr_copy_comma", CASE_INT, "int main() { union U { int x; char y; }; union U v={7}; union U u=(v.x=9,v); return u.x; }", 9, 0},
    {"type_decl", "struct_padding_array", CASE_INT, "int main() { struct S { char c; int x; }; struct S a[2]; a[0].x=3; a[1].c=9; return a[0].x; }", 3, 0},
    {"type_decl", "typedef_int", CASE_INT, "typedef int myint; int main() { myint x=9; return x; }", 9, 0},
    {"type_decl", "typedef_ptr", CASE_INT, "typedef int *intptr; int main() { int a=11; intptr p=&a; return *p; }", 11, 0},
    {"type_decl", "typedef_in_func", CASE_INT, "int main() { typedef int myint; myint x=6; return x; }", 6, 0},
    {"type_decl", "typedef_funcptr", CASE_INT, "typedef int (*fp_t)(int); int main() { fp_t p; return 0; }", 0, 0},
    {"type_decl", "typedef_funcptr_nested", CASE_INT, "typedef int (((*fp_t)))(int); int main() { fp_t p; return 0; }", 0, 0},
    {"type_decl", "typedef_funcptr_param", CASE_INT, "typedef int (*fp_t)(int); int dbl(int x){return x*2;} int apply(fp_t f,int x){return f(x);} int main(){return apply(dbl,7);}", 14, 0},
    {"type_decl", "typedef_ret_funcdef", CASE_INT, "typedef long mylong; mylong add(mylong a, mylong b) { return a+b; } int main() { return (int)add(3,4); }", 7, 0},
    {"type_decl", "typedef_ret_proto", CASE_INT, "typedef long size_t; size_t strlen(const char *s); int main() { return (int)strlen(\"hello\"); }", 5, 0},
    {"type_decl", "typedef_ptr_ret_proto", CASE_INT, "typedef void FILE; FILE *get_null(void); int main() { return 0; }", 0, 0},
    {"type_decl", "unsigned_long_ret_funcdef", CASE_INT, "unsigned long foo(int x) { return (unsigned long)x; } int main() { return (int)foo(42); }", 42, 0},
    {"type_decl", "unsigned_long_decl", CASE_INT, "int main() { unsigned long v=12; return v; }", 12, 0},
    {"type_decl", "unsigned_long_long_decl", CASE_INT, "int main() { unsigned long long v=12; return v; }", 12, 0},
    {"type_decl", "signed_short_decl", CASE_INT, "int main() { signed short v=13; return v; }", 13, 0},
    {"type_decl", "signed_char_decl", CASE_INT, "int main() { signed char v=13; return v; }", 13, 0},
    // integer promotion: signed/unsigned 符号拡張 vs zero拡張
    {"type_decl", "char_sign_extend", CASE_INT, "int main() { char c = 255; return (c < 0) ? 1 : 0; }", 1, 0},
    {"type_decl", "unsigned_char_zero_extend", CASE_INT, "int main() { unsigned char c = 255; return c; }", 255, 0},
    {"type_decl", "short_sign_extend", CASE_INT, "int main() { short s = 65535; return (s < 0) ? 1 : 0; }", 1, 0},
    {"type_decl", "unsigned_short_zero_extend", CASE_INT, "int main() { unsigned short s = 200; return s; }", 200, 0},
    // unsigned演算セマンティクス
    {"type_decl", "unsigned_div", CASE_INT, "int main() { unsigned int a = 100; unsigned int b = 7; return a / b; }", 14, 0},
    {"type_decl", "unsigned_mod", CASE_INT, "int main() { unsigned int a = 100; unsigned int b = 7; return a % b; }", 2, 0},
    {"type_decl", "unsigned_shr", CASE_INT, "int main() { unsigned int a = 0x80000000; return (a >> 31); }", 1, 0},
    {"type_decl", "signed_shr_preserve", CASE_INT, "int main() { long a = -2; return (int)(a >> 1); }", 255, 0},
    {"type_decl", "unsigned_cmp_lt", CASE_INT, "int main() { unsigned int a = (1u << 31) | 1; return (a > 1u) ? 1 : 0; }", 1, 0},
    {"type_decl", "unsigned_cmp_le", CASE_INT, "int main() { unsigned int a = (1u << 31) | 1; unsigned int b = a; return (a <= b) ? 1 : 0; }", 1, 0},
    {"type_decl", "const_decl", CASE_INT, "int main() { const int x=8; return x; }", 8, 0},
    {"type_decl", "volatile_decl", CASE_INT, "int main() { volatile int x=9; return x; }", 9, 0},
    {"type_decl", "storage_specs_local", CASE_INT, "int main() { static int x=8; register int r=2; auto int a=1; int *restrict p=0; return x+r+a+(p==0); }", 12, 0},
    {"type_decl", "scalar_brace_init", CASE_INT, "int main() { int x={3}; return x; }", 3, 0},
    {"type_decl", "complex_sizeof", CASE_INT, "int main() { return sizeof(_Complex double); }", 16, 0},
    {"type_decl", "complex_float_sizeof", CASE_INT, "int main() { return sizeof(_Complex float); }", 8, 0},
    {"type_decl", "complex_init_copy", CASE_INT, "int main() { _Complex double a = 3.0; _Complex double b = a; _Complex double c = 3.0; return b == c; }", 1, 0},
    {"type_decl", "complex_add", CASE_INT, "int main() { _Complex double a = 3.0; _Complex double b = 2.0; _Complex double c = a + b; _Complex double d = 5.0; return c == d; }", 1, 0},
    {"type_decl", "complex_sub", CASE_INT, "int main() { _Complex double a = 5.0; _Complex double b = 3.0; _Complex double c = a - b; _Complex double d = 2.0; return c == d; }", 1, 0},
    {"type_decl", "complex_mul", CASE_INT, "int main() { _Complex double a = 3.0; _Complex double b = 4.0; _Complex double c = a * b; _Complex double d = 12.0; return c == d; }", 1, 0},
    {"type_decl", "complex_div", CASE_INT, "int main() { _Complex double a = 10.0; _Complex double b = 2.0; _Complex double c = a / b; _Complex double d = 5.0; return c == d; }", 1, 0},
    {"type_decl", "complex_ne", CASE_INT, "int main() { _Complex double a = 3.0; _Complex double b = 4.0; return a != b; }", 1, 0},
    {"type_decl", "extern_inline_funcspec", CASE_INT, "extern int g; inline int add(int a, int b) { return a+b; } int main() { return add(3,4); }", 7, 0},
    {"type_decl", "noreturn_spec_parse", CASE_INT, "_Noreturn void die() { return; } int main() { return 7; }", 7, 0},
    {"type_decl", "static_assert_toplevel", CASE_INT, "_Static_assert(1, \"ok\"); int main() { return 7; }", 7, 0},
    {"type_decl", "static_assert_stmt", CASE_INT, "int main() { _Static_assert(1, \"ok\"); return 7; }", 7, 0},
    {"type_decl", "alignas_atomic_prefix", CASE_INT, "int main() { _Alignas(16) int x=3; _Atomic int y=4; return x+y; }", 7, 0},
    {"type_decl", "atomic_type_spec", CASE_INT, "int main() { _Atomic(int) z=5; return z; }", 5, 0},
    {"type_decl", "atomic_load_store", CASE_INT, "int main() { _Atomic int x=10; int y=x+32; return y; }", 42, 0},
    {"type_decl", "generic_int", CASE_INT, "int main() { return _Generic(1, int:11, default:22); }", 11, 0},
    {"type_decl", "generic_double", CASE_INT, "int main() { return _Generic(1.0, float:11, double:33, default:22); }", 33, 0},
    {"type_decl", "generic_ptr", CASE_INT, "int main() { int *p=0; return _Generic(p, int*:3, default:7); }", 3, 0},
    {"type_decl", "const_param", CASE_INT, "int id(const int x) { return x; } int main(){ return id(7); }", 7, 0},
    {"type_decl", "compound_literal_int", CASE_INT, "int main(){ return (int){3}; }", 3, 0},
    {"type_decl", "compound_literal_struct_stmt", CASE_INT, "int main(){ struct S { int x; int y; }; (struct S){1,2}; return 7; }", 7, 0},
    {"type_decl", "compound_literal_struct_member", CASE_INT, "int main(){ struct S { int x; int y; }; return ((struct S){.x=1,.y=2}).y; }", 2, 0},
    {"type_decl", "compound_literal_struct_addr_arrow", CASE_INT, "int main(){ struct S { int x; }; return (&(struct S){3})->x; }", 3, 0},
    {"type_decl", "compound_literal_array_subscript", CASE_INT, "int main(){ return ((int[2]){1,2})[1]; }", 2, 0},
    {"type_decl", "compound_literal_array_subscript0", CASE_INT, "int main(){ return ((int[3]){10,20,30})[0]; }", 10, 0},
    {"type_decl", "compound_literal_array_subscript2", CASE_INT, "int main(){ return ((int[3]){10,20,30})[2]; }", 30, 0},
    // 外側括弧なし: unary() 内で直接 apply_postfix(ref) を呼ぶパス
    {"type_decl", "compound_literal_array_subscript_direct", CASE_INT, "int main(){ return (int[3]){7,8,9}[2]; }", 9, 0},
    // designator 初期化子との組み合わせ
    {"type_decl", "compound_literal_array_subscript_designator", CASE_INT, "int main(){ return ((int[4]){[2]=99})[2]; }", 99, 0},
    // 式中での複数利用
    {"type_decl", "compound_literal_array_subscript_expr", CASE_INT, "int main(){ return ((int[2]){3,4})[0] + ((int[2]){3,4})[1]; }", 7, 0},
    // ファイルスコープ複合リテラル（静的ストレージ期間）
    {"type_decl", "compound_literal_file_scope", CASE_INT, "int x = (int){42}; int main(){ return x; }", 42, 0},
    {"type_decl", "float1", CASE_FLOAT, "float ag_m() { float f = 7; return f; }", 0, 7.0},
    {"type_decl", "float2", CASE_FLOAT, "float ag_m() { float f = 3.14; float g = 4.2; return f + g; }", 0, 7.34},
    {"type_decl", "float3", CASE_FLOAT, "float ag_m() { float f = 5.5; float g = 3.2; return f - g; }", 0, 2.3},
    {"type_decl", "float4", CASE_FLOAT, "float ag_m() { float f = 6.0f; float g = 2.5f; return f * g; }", 0, 15.0},
    {"type_decl", "float5", CASE_FLOAT, "float ag_m() { float f = 10.5F; float g = 3.0F; return f / g; }", 0, 3.5},
    {"type_decl", "double1", CASE_DOUBLE, "double ag_m() { double d = 3.99; return d; }", 0, 3.99},
    {"type_decl", "double2", CASE_DOUBLE, "double ag_m() { double a = 3.1; double b = 4.2; return a + b; }", 0, 7.3},
    {"type_decl", "double3", CASE_DOUBLE, "double ag_m() { double a = 5.0; double b = 3.0; return a * b; }", 0, 15.0},
    {"type_decl", "double4", CASE_DOUBLE, "double ag_m() { double a = 15.0; double b = 3.0; return a / b; }", 0, 5.0},
    // hex float literals (C11 6.4.4.2)
    {"type_decl", "hex_float_double", CASE_DOUBLE, "double ag_m() { double d = 0x1.8p+3; return d; }", 0, 12.0},
    {"type_decl", "hex_float_no_sign", CASE_DOUBLE, "double ag_m() { double d = 0x1p4; return d; }", 0, 16.0},
    {"type_decl", "hex_float_neg_exp", CASE_DOUBLE, "double ag_m() { double d = 0x1p-2; return d; }", 0, 0.25},
    {"type_decl", "hex_float_suffix_f", CASE_FLOAT, "float ag_m() { float f = 0x1.8p+3f; return f; }", 0, 12.0},

    {"pointer", "deref", CASE_INT, "int main() { int x = 5; int *p = &x; return *p; }", 5, 0},
    {"pointer", "assign", CASE_INT, "int main() { int x = 5; int *p = &x; *p = 10; return x; }", 10, 0},
    {"pointer", "arith_add", CASE_INT, "int main() { int a[4]; a[0]=10; a[1]=20; a[2]=30; a[3]=40; int *p=a; p=p+2; return *p; }", 30, 0},
    {"pointer", "arith_sub", CASE_INT, "int main() { int a[4]; a[0]=10; a[1]=20; a[2]=30; a[3]=40; int *p=a; p=p+3; p=p-1; return *p; }", 30, 0},
    {"pointer", "arith_char", CASE_INT, "int main() { char b[4]; b[0]=1; b[1]=2; b[2]=3; b[3]=4; char *p=b; p=p+2; return *p; }", 3, 0},

    {"array", "idx", CASE_INT, "int main() { int arr[3]; arr[0]=1; arr[1]=2; arr[2]=3; return arr[2]; }", 3, 0},
    {"array", "brace_init", CASE_INT, "int main() { int arr[3]={1,2,3}; return arr[2]; }", 3, 0},
    {"array", "brace_init_designated", CASE_INT, "int main() { int arr[4]={[2]=7,[0]=1}; return arr[0]+arr[2]; }", 8, 0},
    {"array", "char_array_string_init", CASE_INT, "int main() { char s[4]=\"abc\"; return s[2]+s[3]; }", 99, 0},
    {"array", "sum", CASE_INT, "int main() { int arr[3]; arr[0]=1; arr[1]=2; arr[2]=3; return arr[0]+arr[1]+arr[2]; }", 6, 0},
    {"array", "const_expr_size", CASE_INT, "int main() { int arr[1+2]; arr[0]=1; arr[1]=2; arr[2]=3; return arr[2]; }", 3, 0},
    {"array", "multi_dim_decl", CASE_INT, "int main() { int arr[2][3]; return 7; }", 7, 0},
    {"array", "multi_dim_init", CASE_INT, "int main() { int a[2][3]={{1,2,3},{4,5,6}}; return a[1][2]; }", 6, 0},
    {"array", "multi_dim_init_sum", CASE_INT, "int main() { int a[2][3]={{1,2,3},{4,5,6}}; return a[0][0]+a[1][2]; }", 7, 0},
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
    // ビットフィールド
    {"bitfield", "read",   CASE_INT, "int main(){ struct S { unsigned int a:3; unsigned int b:5; }; struct S s; s.a=5; s.b=10; return s.a; }", 5, 0},
    {"bitfield", "read_b", CASE_INT, "int main(){ struct S { unsigned int a:3; unsigned int b:5; }; struct S s; s.a=5; s.b=10; return s.b; }", 10, 0},
    {"bitfield", "write_masked", CASE_INT, "int main(){ struct S { unsigned int f:4; }; struct S s; s.f=15; return s.f; }", 15, 0},
    {"bitfield", "packing", CASE_INT, "int main(){ struct S { unsigned int a:3; unsigned int b:5; int c; }; return (int)sizeof(struct S); }", 8, 0},
    {"bitfield", "signed_neg", CASE_INT, "int main(){ struct S { int f:4; }; struct S s; s.f=-1; return (s.f < 0) ? 42 : 0; }", 42, 0},
    {"bitfield", "unsigned_wrap", CASE_INT, "int main(){ struct S { unsigned int f:3; }; struct S s; s.f=9; return s.f; }", 1, 0},
    // _Alignas
    {"alignas", "lvar_value",  CASE_INT, "int main() { _Alignas(16) int a = 42; return a; }", 42, 0},
    {"alignas", "lvar_align",  CASE_INT, "int main() { int pad = 1; _Alignas(16) int a = 42; long addr = (long)&a; return addr % 16 == 0 ? a : 0; }", 42, 0},
    {"alignas", "struct_member", CASE_INT, "int main() { struct S { char pad; _Alignas(8) int x; }; return (int)sizeof(struct S) == 16 ? 42 : 0; }", 42, 0},
    // フレキシブル配列メンバー
    {"flex_array", "sizeof_flex", CASE_INT, "int main() { struct F { int len; int data[]; }; return (int)sizeof(struct F); }", 4, 0},
    {"flex_array", "parse_ok", CASE_INT, "int main() { struct F { int n; char buf[]; }; return 0; }", 0, 0},
    // #pragma pack
    {"pragma_pack", "pack1_sizeof", CASE_INT,
     "#pragma pack(push, 1)\n"
     "struct S { char a; int b; };\n"
     "#pragma pack(pop)\n"
     "int main() { return (int)sizeof(struct S); }",
     5, 0},
    {"pragma_pack", "pack1_offset", CASE_INT,
     "#pragma pack(push, 1)\n"
     "struct S { char a; int b; };\n"
     "#pragma pack(pop)\n"
     "int main() { struct S s; s.a = 1; s.b = 41; return s.a + s.b; }",
     42, 0},
    {"pragma_pack", "pack2_sizeof", CASE_INT,
     "#pragma pack(push, 2)\n"
     "struct S { char a; int b; };\n"
     "#pragma pack(pop)\n"
     "int main() { return (int)sizeof(struct S); }",
     6, 0},
    {"pragma_pack", "pop_restores", CASE_INT,
     "#pragma pack(push, 1)\n"
     "#pragma pack(pop)\n"
     "struct S { char a; int b; };\n"
     "int main() { return (int)sizeof(struct S); }",
     8, 0},
    {"pragma_pack", "pack_n_no_push", CASE_INT,
     "#pragma pack(1)\n"
     "struct S { char a; int b; };\n"
     "#pragma pack()\n"
     "int main() { return (int)sizeof(struct S); }",
     5, 0},
    // 標準ヘッダ
    {"stdheader", "stdint_int32", CASE_INT, "#include <stdint.h>\nint main() { int32_t x = 42; return x; }", 42, 0},
    {"stdheader", "stdint_uint8", CASE_INT, "#include <stdint.h>\nint main() { uint8_t x = 200; return (int)x; }", 200, 0},
    {"stdheader", "stdbool_true", CASE_INT, "#include <stdbool.h>\nint main() { bool b = true; return b ? 42 : 0; }", 42, 0},
    {"stdheader", "stdbool_false", CASE_INT, "#include <stdbool.h>\nint main() { bool b = false; return b ? 1 : 0; }", 0, 0},
    {"stdheader", "stddef_size_t", CASE_INT, "#include <stddef.h>\nint main() { size_t x = 10; return (int)x; }", 10, 0},
    {"stdheader", "stddef_null", CASE_INT, "#include <stddef.h>\nint main() { void *p = NULL; return p == NULL ? 42 : 0; }", 42, 0},
    {"stdheader", "limits_int_max", CASE_INT, "#include <limits.h>\nint main() { return INT_MAX == 2147483647 ? 42 : 0; }", 42, 0},
    {"stdheader", "limits_int_min", CASE_INT, "#include <limits.h>\nint main() { return INT_MIN < 0 ? 42 : 0; }", 42, 0},
    {"stdheader", "limits_char_bit", CASE_INT, "#include <limits.h>\nint main() { return CHAR_BIT == 8 ? 42 : 0; }", 42, 0},
    {"stdheader", "float_flt_max", CASE_INT, "#include <float.h>\nint main() { float f = FLT_MAX; return f > 1.0F ? 42 : 0; }", 42, 0},
    {"stdheader", "float_dbl_epsilon", CASE_INT, "#include <float.h>\nint main() { double e = DBL_EPSILON; return e > 0.0 && e < 1.0 ? 42 : 0; }", 42, 0},
    {"stdheader", "float_flt_radix", CASE_INT, "#include <float.h>\nint main() { return FLT_RADIX == 2 ? 42 : 0; }", 42, 0},
    {"stdheader", "string_strlen", CASE_INT, "#include <string.h>\nint main() { return (int)strlen(\"hello\"); }", 5, 0},
    {"stdheader", "string_strcmp", CASE_INT, "#include <string.h>\nint main() { return strcmp(\"abc\", \"abc\") == 0 ? 42 : 0; }", 42, 0},
    {"stdheader", "stdlib_malloc_free", CASE_INT,
     "#include <stdlib.h>\n"
     "int main() { int *p = malloc(8); *p = 42; int v = *p; free(p); return v; }",
     42, 0},
    {"stdheader", "stdlib_atoi", CASE_INT, "#include <stdlib.h>\nint main() { return atoi(\"42\"); }", 42, 0},
    {"stdheader", "stdlib_abs", CASE_INT, "#include <stdlib.h>\nint main() { return abs(-42); }", 42, 0},
    {"stdheader", "string_memset", CASE_INT,
     "#include <string.h>\n"
     "int main() { char buf[4]; memset(buf, 0, 4); return buf[0] == 0 && buf[3] == 0 ? 42 : 0; }",
     42, 0},
    {"stdheader", "ctype_isdigit", CASE_INT, "#include <ctype.h>\nint main() { return isdigit('5') != 0 ? 42 : 0; }", 42, 0},
    {"stdheader", "ctype_isalpha", CASE_INT, "#include <ctype.h>\nint main() { return isalpha('A') != 0 ? 42 : 0; }", 42, 0},
    {"stdheader", "ctype_toupper", CASE_INT, "#include <ctype.h>\nint main() { return toupper('a'); }", 65, 0},
    {"stdheader", "math_include", CASE_INT, "#include <math.h>\nint main() { return 42; }", 42, 0},
    {"stdheader", "assert_include", CASE_INT, "#include <assert.h>\nint main() { assert(1); return 42; }", 42, 0},
    {"stdheader", "errno_include", CASE_INT, "#include <errno.h>\nint main() { return EDOM == 33 ? 42 : 0; }", 42, 0},
    {"stdheader", "signal_include", CASE_INT, "#include <signal.h>\nint main() { return SIGINT == 2 ? 42 : 0; }", 42, 0},
    {"stdheader", "time_include", CASE_INT, "#include <time.h>\nint main() { time_t t = 0; return 42; }", 42, 0},
    {"stdheader", "setjmp_include", CASE_INT, "#include <setjmp.h>\nint main() { return 42; }", 42, 0},
    // stdarg
    {"stdarg", "va_arg_int", CASE_INT,
     "#include <stdarg.h>\n"
     "int my_sum(int n, ...) {\n"
     "  va_list ap; va_start(ap, n);\n"
     "  int s = 0; int i;\n"
     "  for (i = 0; i < n; i++) { s += va_arg(ap, int); }\n"
     "  va_end(ap); return s;\n"
     "}\n"
     "int main() { return my_sum(3, 10, 20, 12); }",
     42, 0},

    // VLA (Variable Length Array)
    {"vla", "basic_elem", CASE_INT,
     "int main() { int n = 3; int a[n]; a[0] = 10; a[1] = 20; a[2] = 12; return a[0] + a[1] + a[2]; }",
     42, 0},
    {"vla", "loop_fill", CASE_INT,
     "int main() { int n = 5; int a[n]; int i; for (i = 0; i < n; i++) a[i] = i; return a[0] + a[1] + a[2] + a[3] + a[4]; }",
     10, 0},
    {"vla", "param_size", CASE_INT,
     "int sum(int n) { int a[n]; int i; for (i = 0; i < n; i++) a[i] = i + 1; int s = 0; for (i = 0; i < n; i++) s += a[i]; return s; }\n"
     "int main() { return sum(4); }",
     10, 0},
    {"vla", "sizeof_vla", CASE_INT,
     "int main() { int n = 3; int a[n]; return (int)sizeof(a); }",
     12, 0},
    // 構造体引数渡し (ARM64 ABI)
    {"struct_arg", "small_sum", CASE_INT,
     "struct Point { int x; int y; };"
     "int sum(struct Point p) { return p.x + p.y; }"
     "int main() { struct Point pt = {3, 4}; return sum(pt); }",
     7, 0},
    {"struct_arg", "small_member", CASE_INT,
     "struct Point { int x; int y; };"
     "int get_y(struct Point p) { return p.y; }"
     "int main() { struct Point pt = {10, 42}; return get_y(pt); }",
     42, 0},
    {"struct_arg", "mid_sum", CASE_INT,
     "struct Mid { int a; int b; int c; };"
     "int sum3(struct Mid p) { return p.a + p.b + p.c; }"
     "int main() { struct Mid m = {10, 20, 12}; return sum3(m); }",
     42, 0},
    {"struct_arg", "large_sum", CASE_INT,
     "struct Big { int a; int b; int c; int d; int e; };"
     "int sum5(struct Big p) { return p.a + p.b + p.c + p.d + p.e; }"
     "int main() { struct Big b = {1, 2, 3, 4, 5}; return sum5(b); }",
     15, 0},
    // struct return value (≤8B)
    {"struct_ret", "make_and_sum", CASE_INT,
     "struct Point { int x; int y; };"
     "struct Point make_point(int x, int y) { struct Point p = {x, y}; return p; }"
     "int main() { struct Point r = make_point(10, 32); return r.x + r.y; }",
     42, 0},
    {"struct_ret", "return_member", CASE_INT,
     "struct Pair { int a; int b; };"
     "struct Pair swap(int a, int b) { struct Pair p = {b, a}; return p; }"
     "int main() { struct Pair r = swap(7, 35); return r.a + r.b; }",
     42, 0},
    {"struct_ret", "chain_call", CASE_INT,
     "struct Val { int v; };"
     "struct Val make_val(int n) { struct Val v = {n}; return v; }"
     "int get_v(struct Val p) { return p.v; }"
     "int main() { return get_v(make_val(42)); }",
     42, 0},
    // struct return value (9-16B: x0/x1 pair)
    {"struct_ret", "ret_12b_sum", CASE_INT,
     "struct Triple { int a; int b; int c; };"
     "struct Triple make_triple(int x, int y, int z) { struct Triple t = {x, y, z}; return t; }"
     "int main() { struct Triple r = make_triple(10, 20, 12); return r.a + r.b + r.c; }",
     42, 0},
    {"struct_ret", "ret_16b_sum", CASE_INT,
     "struct Quad { int a; int b; int c; int d; };"
     "struct Quad make_quad(int a, int b, int c, int d) { struct Quad q = {a, b, c, d}; return q; }"
     "int main() { struct Quad r = make_quad(1, 2, 3, 36); return r.a + r.b + r.c + r.d; }",
     42, 0},
    {"struct_ret", "ret_12b_member_c", CASE_INT,
     "struct Triple { int a; int b; int c; };"
     "struct Triple make(int x) { struct Triple t = {x, x+1, x+2}; return t; }"
     "int main() { struct Triple r = make(10); return r.c; }",
     12, 0},
    // struct return value (>16B: indirect return via x8)
    {"struct_ret", "ret_20b_indirect", CASE_INT,
     "struct Big { int a; int b; int c; int d; int e; };"
     "struct Big make_big(int v) { struct Big b = {v, v+1, v+2, v+3, v+4}; return b; }"
     "int main() { struct Big r = make_big(5); return r.a + r.b + r.c + r.d + r.e; }",
     35, 0},
    {"struct_ret", "ret_24b_member_f", CASE_INT,
     "struct S6 { int a; int b; int c; int d; int e; int f; };"
     "struct S6 make6(int x) { struct S6 s = {x, x+1, x+2, x+3, x+4, x+5}; return s; }"
     "int main() { struct S6 r = make6(1); return r.f; }",
     6, 0},
    {"struct_ret", "ret_40b_sum", CASE_INT,
     "struct Big10 { int a; int b; int c; int d; int e; int f; int g; int h; int i; int j; };"
     "struct Big10 make10() { struct Big10 s = {1,2,3,4,5,6,7,8,9,10}; return s; }"
     "int main() { struct Big10 r = make10(); return r.a+r.b+r.c+r.d+r.e+r.f+r.g+r.h+r.i+r.j; }",
     55, 0},
    // __func__ 定義済み識別子
    {"func_name", "first_char_main", CASE_INT,
     "int main() { return (int)__func__[0]; }",
     109, 0},  // 'm' == 109
    {"func_name", "first_char_helper", CASE_INT,
     "int helper() { return (int)__func__[0]; }"
     "int main() { return helper(); }",
     104, 0},  // 'h' == 104
    {"func_name", "each_func_distinct", CASE_INT,
     "int fa() { return (int)__func__[1]; }"  // __func__[1]=='a'==97
     "int fb() { return (int)__func__[1]; }"  // __func__[1]=='b'==98
     "int main() { return fb() - fa() + 41; }",  // (98-97)+41 == 42
     42, 0},
    // 2D VLA: constant inner dimension
    {"vla_2d", "const_inner_read", CASE_INT,
     "int main() {"
     "  int n = 3; int a[n][4];"
     "  a[0][0]=1; a[0][1]=2; a[1][0]=10; a[2][3]=100;"
     "  return a[0][0]+a[0][1]+a[1][0]+a[2][3];"   // 1+2+10+100=113 → mod256=113
     "}", 113, 0},
    {"vla_2d", "const_inner_loop", CASE_INT,
     "int main() {"
     "  int n = 2; int sum = 0; int a[n][3];"
     "  int i; for (i=0;i<n;i++) { int j; for(j=0;j<3;j++) a[i][j]=i*3+j; }"
     "  for (i=0;i<n;i++) { int j; for(j=0;j<3;j++) sum+=a[i][j]; }"
     "  return sum;"   // 0+1+2+3+4+5=15
     "}", 15, 0},
    // 2D VLA: runtime inner dimension
    {"vla_2d", "runtime_inner_read", CASE_INT,
     "int main() {"
     "  int n = 2; int m = 3; int a[n][m];"
     "  a[0][0]=10; a[0][2]=5; a[1][1]=20; a[1][2]=7;"
     "  return a[0][0]+a[0][2]+a[1][1]+a[1][2];"   // 10+5+20+7=42
     "}", 42, 0},
    {"vla_2d", "runtime_inner_loop", CASE_INT,
     "int main() {"
     "  int n = 3; int m = 4; int sum = 0; int a[n][m];"
     "  int i; for(i=0;i<n;i++) { int j; for(j=0;j<m;j++) a[i][j]=i*m+j; }"
     "  for(i=0;i<n;i++) { int j; for(j=0;j<m;j++) sum+=a[i][j]; }"
     "  return sum;"   // 0..11 sum=66
     "}", 66, 0},
    // 仮引数 VLA 宣言子: int a[n] → int *a (C11 6.7.6.3p7)
    {"vla_param", "basic_access", CASE_INT,
     "int sum_arr(int n, int a[n]) { int s=0; int i; for(i=0;i<n;i++) s+=a[i]; return s; }"
     "int main() { int n=5; int a[n]; int i; for(i=0;i<n;i++) a[i]=i+1; return sum_arr(n,a); }",
     15, 0},  // 1+2+3+4+5=15
    {"vla_param", "sizeof_is_ptr", CASE_INT,
     "int get_size(int n, int a[n]) { return (int)sizeof(a); }"
     "int main() { int n=10; int a[n]; return get_size(n,a); }",
     8, 0},  // sizeof(pointer)==8
    {"vla_param", "write_through", CASE_INT,
     "void fill(int n, int a[n], int v) { int i; for(i=0;i<n;i++) a[i]=v; }"
     "int main() { int n=3; int a[n]; fill(n,a,14); return a[0]+a[1]+a[2]; }",
     42, 0},  // 14*3=42
    // inline 指定子: 単一翻訳単位では通常関数と同様にコード生成 (C11 6.7.4)
    {"inline_func", "basic_inline", CASE_INT,
     "inline int add(int a, int b) { return a + b; }"
     "int main() { return add(20, 22); }",
     42, 0},
    {"inline_func", "static_inline", CASE_INT,
     "static inline int mul(int a, int b) { return a * b; }"
     "int main() { return mul(6, 7); }",
     42, 0},
    {"inline_func", "extern_inline", CASE_INT,
     "extern inline int sub(int a, int b) { return a - b; }"
     "int main() { return sub(50, 8); }",
     42, 0},
    // グローバル変数: 暫定定義
    {"global_var", "tentative_rw", CASE_INT,
     "int g; int main() { g = 42; return g; }",
     42, 0},
    {"global_var", "tentative_multi_func", CASE_INT,
     "int g; int set_g(int v) { g = v; return 0; } int main() { set_g(42); return g; }",
     42, 0},
    // グローバル変数: 初期化済み定義
    {"global_var", "initialized", CASE_INT,
     "int g = 42; int main() { return g; }",
     42, 0},
    {"global_var", "initialized_modified", CASE_INT,
     "int g = 10; int main() { g = g + 32; return g; }",
     42, 0},
    // ローカルスコープのextern宣言
    {"global_var", "local_extern", CASE_INT,
     "int g = 42; int main() { extern int g; return g; }",
     42, 0},
    {"global_var", "array_rw", CASE_INT,
     "int g[3]; int main() { g[0]=10; g[1]=20; g[2]=30; return g[1]; }",
     20, 0},
    {"global_var", "array_sum", CASE_INT,
     "int g[3]; int main() { g[0]=1; g[1]=2; g[2]=3; return g[0]+g[1]+g[2]; }",
     6, 0},
};

static const compile_fail_case_t compile_fail_cases[] = {
    {"cast_struct_from_nonscalar_rejected",
     "int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }",
     "[cast] struct 値へのキャストは未対応です（型不整合）"},
    {"const_assign_rejected",
     "int main() { const int x = 5; x = 10; return 0; }",
     "const修飾された変数への代入はできません"},
    {"const_compound_assign_rejected",
     "int main() { const int x = 5; x += 1; return 0; }",
     "const修飾された変数への代入はできません"},
    {"const_increment_rejected",
     "int main() { const int x = 5; x++; return 0; }",
     "const修飾された変数への代入はできません"},
    {"const_qual_discard_init_rejected",
     "int main() { const int x = 5; const int *cp = &x; int *p = cp; return 0; }",
     "const修飾されたポインタからconst無しポインタへの暗黙変換はできません"},
    {"const_qual_discard_assign_rejected",
     "int main() { const int x = 5; const int *cp = &x; int *p; p = cp; return 0; }",
     "const修飾されたポインタからconst無しポインタへの暗黙変換はできません"},
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

static int run_ag_c_expect_fail_with_diag(const char *input, const char *expected_diag,
                                          const char *log_path) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    freopen("/dev/null", "w", stdout);
    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    _exit(1);
  }
  close(pipefd[1]);

  char diag_buf[8192];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(pipefd[0], diag_buf + used, sizeof(diag_buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(diag_buf) - 1) break;
  }
  close(pipefd[0]);
  diag_buf[used] = '\0';

  int status;
  waitpid(pid, &status, 0);

  FILE *log = fopen(log_path, "w");
  if (log) {
    fputs(diag_buf, log);
    fclose(log);
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) return -1;
  if (!strstr(diag_buf, expected_diag)) return -1;
  return 0;
}

static int run_clang_build_many(const char *bin_path, const char **inputs, size_t ninputs) {
  pid_t pid = fork();
  if (pid == 0) {
    char **argv = calloc(ninputs + 4, sizeof(char *));
    if (!argv) _exit(1);
    argv[0] = "clang";
    argv[1] = "-o";
    argv[2] = (char *)bin_path;
    for (size_t i = 0; i < ninputs; i++) {
      argv[3 + i] = (char *)inputs[i];
    }
    argv[3 + ninputs] = NULL;
    execvp("clang", argv);
    _exit(1);
  }
  if (pid < 0) return -1;
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

static void build_category_bin_path(const char *category, char *bin_path) {
  snprintf(bin_path, PATH_MAX, "build/e2e/%s/%s_category_runner", category, category);
}

static void sanitize_symbol(const char *in, char *out, size_t out_size) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < out_size; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out[j++] = (char)c;
    } else {
      out[j++] = '_';
    }
  }
  out[j] = '\0';
}

static int copy_and_namespace_symbols(const char *src_path, const char *dst_path, const char *prefix) {
  FILE *in = fopen(src_path, "r");
  if (!in) return -1;
  FILE *out = fopen(dst_path, "w");
  if (!out) {
    fclose(in);
    return -1;
  }

  char *line = NULL;
  size_t cap = 0;
  while (getline(&line, &cap, in) != -1) {
    size_t len = strlen(line);
    for (size_t i = 0; i < len; ) {
      if (line[i] == '_' && (i == 0 || line[i - 1] != '_') && i + 1 < len &&
          ((line[i + 1] >= 'A' && line[i + 1] <= 'Z') || (line[i + 1] >= 'a' && line[i + 1] <= 'z'))) {
        size_t j = i + 1;
        while ((line[j] >= 'A' && line[j] <= 'Z') || (line[j] >= 'a' && line[j] <= 'z') ||
               (line[j] >= '0' && line[j] <= '9') || line[j] == '_') {
          j++;
        }
        char sym[256];
        size_t sym_len = j - i;
        if (sym_len >= sizeof(sym)) sym_len = sizeof(sym) - 1;
        memcpy(sym, line + i, sym_len);
        sym[sym_len] = '\0';
        if (strcmp(sym, "_printf") == 0 || (sym[1] == '_') ||
            strcmp(sym, "_TEXT") == 0 || strcmp(sym, "_cstring") == 0 ||
            strcmp(sym, "_literal4") == 0 || strcmp(sym, "_literal8") == 0 ||
            strcmp(sym, "_literal16") == 0 || strcmp(sym, "_const") == 0 ||
            strcmp(sym, "_strcmp") == 0 || strcmp(sym, "_strncmp") == 0 ||
            strcmp(sym, "_strlen") == 0 || strcmp(sym, "_strcpy") == 0 ||
            strcmp(sym, "_strncpy") == 0 || strcmp(sym, "_strcat") == 0 ||
            strcmp(sym, "_strncat") == 0 || strcmp(sym, "_strchr") == 0 ||
            strcmp(sym, "_strrchr") == 0 || strcmp(sym, "_strstr") == 0 ||
            strcmp(sym, "_strtok") == 0 || strcmp(sym, "_strerror") == 0 ||
            strcmp(sym, "_memcpy") == 0 || strcmp(sym, "_memmove") == 0 ||
            strcmp(sym, "_memcmp") == 0 || strcmp(sym, "_memchr") == 0 ||
            strcmp(sym, "_memset") == 0 ||
            strcmp(sym, "_malloc") == 0 || strcmp(sym, "_calloc") == 0 ||
            strcmp(sym, "_realloc") == 0 || strcmp(sym, "_free") == 0 ||
            strcmp(sym, "_exit") == 0 || strcmp(sym, "_abort") == 0 ||
            strcmp(sym, "_atoi") == 0 || strcmp(sym, "_atol") == 0 ||
            strcmp(sym, "_puts") == 0 || strcmp(sym, "_fprintf") == 0 ||
            strcmp(sym, "_sprintf") == 0 || strcmp(sym, "_snprintf") == 0 ||
            strcmp(sym, "_va_start") == 0 || strcmp(sym, "_va_end") == 0 ||
            strcmp(sym, "_abs") == 0 || strcmp(sym, "_labs") == 0 ||
            strcmp(sym, "_rand") == 0 || strcmp(sym, "_srand") == 0 ||
            strcmp(sym, "_qsort") == 0 || strcmp(sym, "_bsearch") == 0 ||
            strcmp(sym, "_atexit") == 0 || strcmp(sym, "_getenv") == 0 ||
            strcmp(sym, "_system") == 0 || strcmp(sym, "_strtol") == 0 ||
            strcmp(sym, "_perror") == 0 || strcmp(sym, "_fopen") == 0 ||
            strcmp(sym, "_fclose") == 0 || strcmp(sym, "_fflush") == 0 ||
            strcmp(sym, "_fread") == 0 || strcmp(sym, "_fwrite") == 0 ||
            strcmp(sym, "_fputs") == 0 || strcmp(sym, "_fputc") == 0 ||
            strcmp(sym, "_fgetc") == 0 || strcmp(sym, "_fgets") == 0 ||
            strcmp(sym, "_getchar") == 0 || strcmp(sym, "_putchar") == 0 ||
            strcmp(sym, "_isalnum") == 0 || strcmp(sym, "_isalpha") == 0 ||
            strcmp(sym, "_isblank") == 0 || strcmp(sym, "_iscntrl") == 0 ||
            strcmp(sym, "_isdigit") == 0 || strcmp(sym, "_isgraph") == 0 ||
            strcmp(sym, "_islower") == 0 || strcmp(sym, "_isprint") == 0 ||
            strcmp(sym, "_ispunct") == 0 || strcmp(sym, "_isspace") == 0 ||
            strcmp(sym, "_isupper") == 0 || strcmp(sym, "_isxdigit") == 0 ||
            strcmp(sym, "_tolower") == 0 || strcmp(sym, "_toupper") == 0) {
          fputs(sym, out);
        } else {
          fprintf(out, "_%s_%s", prefix, sym + 1);
        }
        i = j;
      } else {
        fputc(line[i], out);
        i++;
      }
    }
  }
  free(line);
  fclose(in);
  fclose(out);
  return 0;
}

static int build_category(const char *category) {
  char log_path[PATH_MAX];
  char driver_path[PATH_MAX];
  char bin_path[PATH_MAX];
  char category_dir[PATH_MAX];
  snprintf(log_path, sizeof(log_path), "build/e2e/logs/%s.build.log", category);
  snprintf(driver_path, sizeof(driver_path), "build/e2e/%s/%s_category_driver.c", category, category);
  snprintf(category_dir, sizeof(category_dir), "build/e2e/%s", category);
  build_category_bin_path(category, bin_path);

  FILE *log = fopen(log_path, "w");
  if (!log) return -1;
  fprintf(log, "Category: %s\n", category);

  const size_t max_cases = sizeof(test_cases) / sizeof(test_cases[0]);
  const char **clang_inputs = calloc(max_cases + 1, sizeof(char *));
  char **owned_paths = calloc(max_cases + 1, sizeof(char *));
  if (!clang_inputs || !owned_paths) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }
  if (mkdir_p(category_dir) != 0) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }

  FILE *drv = fopen(driver_path, "w");
  if (!drv) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }

  fprintf(drv, "#include <math.h>\n#include <stdio.h>\n\n");
  fprintf(drv, "static int agc_nearly(double a, double b) { return fabs(a - b) < 0.001; }\n\n");
  fprintf(drv, "int main(void) {\n  int failed = 0;\n");

  size_t input_count = 0;
  for (size_t i = 0; i < max_cases; i++) {
    const test_case_t *tc = &test_cases[i];
    if (strcmp(tc->category, category) != 0) continue;

    char dir[PATH_MAX], s_path[PATH_MAX], bin_unused[PATH_MAX], drv_unused[PATH_MAX], src_path[PATH_MAX], rs_path[PATH_MAX];
    build_artifact_paths(tc, dir, s_path, bin_unused, drv_unused);
    build_source_path(tc, src_path);
    snprintf(rs_path, sizeof(rs_path), "%s/%s.renamed.s", dir, tc->name);

    if (mkdir_p(dir) != 0 || write_source_file(src_path, tc->input) != 0 || run_ag_c_to_s(src_path, s_path) != 0) {
      fprintf(log, "  FAIL: build %s\n  input: %s\n  artifacts: s=%s\n", tc->name, tc->input, s_path);
      fclose(drv);
      fclose(log);
      free(clang_inputs);
      free(owned_paths);
      return 1;
    }

    char cat_sym[128], name_sym[128], fn_sym[320];
    sanitize_symbol(category, cat_sym, sizeof(cat_sym));
    sanitize_symbol(tc->name, name_sym, sizeof(name_sym));
    snprintf(fn_sym, sizeof(fn_sym), "agc_%s_%s", cat_sym, name_sym);
    if (copy_and_namespace_symbols(s_path, rs_path, fn_sym) != 0) {
      fprintf(log, "  FAIL: rewrite %s\n", tc->name);
      fclose(drv);
      fclose(log);
      free(clang_inputs);
      free(owned_paths);
      return 1;
    }

    owned_paths[input_count] = strdup(rs_path);
    clang_inputs[input_count] = owned_paths[input_count];
    if (!owned_paths[input_count]) {
      fclose(drv);
      fclose(log);
      free(clang_inputs);
      free(owned_paths);
      return 1;
    }
    input_count++;

    if (tc->kind == CASE_INT) {
      fprintf(drv, "  extern int %s_main(void);\n", fn_sym);
    } else if (tc->kind == CASE_DOUBLE) {
      fprintf(drv, "  extern double %s_ag_m(void);\n", fn_sym);
    } else {
      fprintf(drv, "  extern float %s_ag_m(void);\n", fn_sym);
    }
  }

  fprintf(drv, "\n");
  for (size_t i = 0; i < max_cases; i++) {
    const test_case_t *tc = &test_cases[i];
    if (strcmp(tc->category, category) != 0) continue;
    char cat_sym[128], name_sym[128], fn_sym[320];
    sanitize_symbol(category, cat_sym, sizeof(cat_sym));
    sanitize_symbol(tc->name, name_sym, sizeof(name_sym));
    snprintf(fn_sym, sizeof(fn_sym), "agc_%s_%s", cat_sym, name_sym);

    if (tc->kind == CASE_INT) {
      fprintf(drv, "  { int actual = (%s_main() & 255); if (actual != %d) { failed = 1; printf(\"FAIL %s expected %d got %%d\\n\", actual); } else { printf(\"OK %s => %%d\\n\", actual); } }\n",
              fn_sym, tc->expected_i, tc->name, tc->expected_i, tc->name);
    } else {
      fprintf(drv, "  { double actual = (double)%s_ag_m(); if (!agc_nearly(actual, %.6f)) { failed = 1; printf(\"FAIL %s expected %.2f got %%.2f\\n\", actual); } else { printf(\"OK %s => %%.2f\\n\", actual); } }\n",
              fn_sym, tc->expected_f, tc->name, tc->expected_f, tc->name);
    }
  }
  fprintf(drv, "  return failed;\n}\n");
  fclose(drv);

  owned_paths[input_count] = strdup(driver_path);
  clang_inputs[input_count] = owned_paths[input_count];
  if (!owned_paths[input_count]) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }
  input_count++;

  if (run_clang_build_many(bin_path, clang_inputs, input_count) != 0) {
    fprintf(log, "  FAIL: clang link category binary\n");
    for (size_t i = 0; i < input_count; i++) free(owned_paths[i]);
    free(clang_inputs);
    free(owned_paths);
    fclose(log);
    return 1;
  }

  for (size_t i = 0; i < input_count; i++) free(owned_paths[i]);
  free(clang_inputs);
  free(owned_paths);
  fprintf(log, "Summary: build OK\n");
  fclose(log);
  return 0;
}

static int run_category(const char *category) {
  char log_path[PATH_MAX];
  char bin_path[PATH_MAX];
  snprintf(log_path, sizeof(log_path), "build/e2e/logs/%s.log", category);
  build_category_bin_path(category, bin_path);
  FILE *log = fopen(log_path, "w");
  if (!log) return -1;
  fprintf(log, "Category: %s\n", category);

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    fclose(log);
    return -1;
  }
  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    execl(bin_path, bin_path, (char *)NULL);
    _exit(1);
  }
  close(pipefd[1]);
  if (pid < 0) {
    close(pipefd[0]);
    fclose(log);
    return -1;
  }

  char buf[1024];
  ssize_t nread = 0;
  while ((nread = read(pipefd[0], buf, sizeof(buf))) > 0) {
    fwrite(buf, 1, (size_t)nread, log);
  }
  close(pipefd[0]);
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(log, "Summary: FAILED\n");
    fclose(log);
    return 1;
  }
  fprintf(log, "Summary: PASS\n");
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

  for (size_t i = 0; i < sizeof(compile_fail_cases) / sizeof(compile_fail_cases[0]); i++) {
    const compile_fail_case_t *tc = &compile_fail_cases[i];
    char src_path[PATH_MAX];
    char log_path[PATH_MAX];
    snprintf(src_path, sizeof(src_path), "build/e2e/compile_fail/%s.c", tc->name);
    snprintf(log_path, sizeof(log_path), "build/e2e/logs/compile_fail_%s.log", tc->name);
    if (mkdir_p("build/e2e/compile_fail") != 0 || write_source_file(src_path, tc->input) != 0 ||
        run_ag_c_expect_fail_with_diag(src_path, tc->expected_diag, log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: %s (see %s)\n", tc->name, log_path);
      return 1;
    }
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

  test_count = (int)((sizeof(test_cases) / sizeof(test_cases[0])) +
                     (sizeof(compile_fail_cases) / sizeof(compile_fail_cases[0])));
  pass_count = failed ? 0 : test_count;

  free(categories);
  free(build_pids);
  free(pids);
  if (failed) return 1;
  printf("OK: All %d E2E tests passed! (%d/%d)\n", test_count, pass_count, test_count);
  return 0;
}
