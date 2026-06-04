/*
 * IR Phase 2: AST→IR→ASM の E2E テスト。
 *
 * AG_USE_IR=1 を立てて ag_c を起動し、IR 経路で生成された ASM が cc 経由で
 * 動くことを確認する。Phase 2 でサポート済みの最小サブセット限定。
 *
 * 失敗パス (サポート外 AST が含まれる場合) は fallback メッセージを stderr に
 * 出した上で AST 経路でビルドされる。ここでは IR 経路で完了するケースのみ扱う。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;
static int total = 0;

static int run_ir_case(const char *label, const char *source, int expected_exit) {
  total++;
  /* 一時ファイル */
  char src_path[64];
  char asm_path[64];
  char bin_path[64];
  snprintf(src_path, sizeof(src_path), "/tmp/ir_e2e_%d.c", total);
  snprintf(asm_path, sizeof(asm_path), "/tmp/ir_e2e_%d.s", total);
  snprintf(bin_path, sizeof(bin_path), "/tmp/ir_e2e_%d", total);

  FILE *fp = fopen(src_path, "w");
  if (!fp) { fprintf(stderr, "FAIL [%s]: cannot write src\n", label); failures++; return 0; }
  fputs(source, fp);
  fclose(fp);

  /* AG_USE_IR=1 で ag_c を起動して asm 生成 */
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "AG_USE_IR=1 ./build/ag_c %s > %s 2>/dev/null", src_path, asm_path);
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "FAIL [%s]: ag_c failed (rc=%d)\n", label, rc);
    failures++;
    return 0;
  }

  /* cc で link */
  snprintf(cmd, sizeof(cmd), "cc -o %s %s 2>/dev/null", bin_path, asm_path);
  rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "FAIL [%s]: cc link failed (rc=%d)\n", label, rc);
    failures++;
    return 0;
  }

  /* 実行 (10 秒タイムアウト)。生成バイナリが無限ループしてもテストが落ちずに
   * 進めるよう、ulimit -t と /usr/bin/timeout のどちらかを使う。 */
  snprintf(cmd, sizeof(cmd),
            "ulimit -t 10; %s", bin_path);
  rc = system(cmd);
  int got = WEXITSTATUS(rc);
  if (got != expected_exit) {
    fprintf(stderr, "FAIL [%s]: exit got=%d expected=%d\n", label, got, expected_exit);
    failures++;
    return 0;
  }
  return 1;
}

int main(void) {
  /* Phase 2 サポート範囲のテスト群 */
  run_ir_case("ret_const",
              "int main(void) { return 42; }\n", 42);
  run_ir_case("arith_add",
              "int main(void) { return 1 + 2; }\n", 3);
  run_ir_case("arith_mul_sub",
              "int main(void) { return 10 * 3 - 5; }\n", 25);
  run_ir_case("arith_div",
              "int main(void) { return 100 / 4; }\n", 25);
  run_ir_case("arith_mod",
              "int main(void) { return 17 % 5; }\n", 2);
  run_ir_case("lvar_simple",
              "int main(void) { int x = 5; return x; }\n", 5);
  run_ir_case("lvar_expr",
              "int main(void) { int x = 5; int y = x * 2 + 1; return y; }\n", 11);
  run_ir_case("lvar_chained",
              "int main(void) { int a = 1; int b = a + 2; int c = b * 3; return c; }\n", 9);
  /* implicit return 0 */
  run_ir_case("no_return",
              "int main(void) { int x = 1 + 2; }\n", 0);

  /* Phase 3: 制御フロー */
  run_ir_case("if_true",
              "int main(void) { int x = 7; if (x < 10) return 1; return 0; }\n", 1);
  run_ir_case("if_false",
              "int main(void) { int x = 20; if (x < 10) return 1; return 2; }\n", 2);
  run_ir_case("if_else",
              "int main(void) { int x = 20; if (x < 10) return 1; else return 2; }\n", 2);
  run_ir_case("while_sum",
              "int main(void) { int i = 1; int s = 0; while (i <= 10) { s = s + i; i = i + 1; } return s; }\n", 55);
  run_ir_case("for_factorial",
              "int main(void) { int n = 5; int r = 1; int i; for (i = 1; i <= n; i = i + 1) r = r * i; return r; }\n", 120);
  run_ir_case("break_loop",
              "int main(void) { int i = 0; while (1) { if (i == 7) break; i = i + 1; } return i; }\n", 7);
  run_ir_case("continue_loop",
              "int main(void) { int i = 0; int s = 0; while (i < 10) { i = i + 1; if (i == 5) continue; s = s + i; } return s; }\n", 50);
  run_ir_case("do_while",
              "int main(void) { int i = 0; do { i = i + 1; } while (i < 5); return i; }\n", 5);
  run_ir_case("nested_for",
              "int main(void) { int s = 0; int i; int j; for (i = 0; i < 3; i = i + 1) for (j = 0; j < 3; j = j + 1) s = s + 1; return s; }\n", 9);
  run_ir_case("eq_ne",
              "int main(void) { int x = 5; if (x == 5) if (x != 0) return 7; return 0; }\n", 7);

  /* Phase 4a: 仮引数 + 直接呼び出し */
  run_ir_case("call_add",
              "int add(int a, int b) { return a + b; } int main(void) { return add(3, 4); }\n", 7);
  run_ir_case("call_nested",
              "int square(int x) { return x * x; } int cube(int x) { return x * square(x); } int main(void) { return cube(3); }\n", 27);
  run_ir_case("call_recursive_fib",
              "int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); } int main(void) { return fib(10); }\n", 55);
  run_ir_case("call_sum8",
              "int sum8(int a, int b, int c, int d, int e, int f, int g, int h) { return a + b + c + d + e + f + g + h; } int main(void) { return sum8(1,2,3,4,5,6,7,8); }\n", 36);
  run_ir_case("call_with_loop",
              "int sum_to(int n) { int s = 0; int i; for (i = 1; i <= n; i = i + 1) s = s + i; return s; } int main(void) { return sum_to(10); }\n", 55);

  /* Phase 4b: ポインタ */
  run_ir_case("ptr_deref",
              "int main(void) { int x = 5; int *p = &x; return *p; }\n", 5);
  run_ir_case("ptr_write",
              "int main(void) { int x = 0; int *p = &x; *p = 42; return x; }\n", 42);
  run_ir_case("ptr_param_inc",
              "void inc(int *p) { *p = *p + 1; } int main(void) { int x = 5; inc(&x); inc(&x); inc(&x); return x; }\n", 8);
  run_ir_case("ptr_swap",
              "void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; } int main(void) { int x = 3; int y = 7; swap(&x, &y); return x * 10 + y; }\n", 73);
  run_ir_case("ptr_via_pointer",
              "int read_int(int *p) { return *p; } int main(void) { int x = 99; return read_int(&x); }\n", 99);

  /* Phase 4c: 配列 */
  run_ir_case("arr_1d",
              "int main(void) { int arr[3]; arr[0] = 10; arr[1] = 20; arr[2] = 12; return arr[0] + arr[1] + arr[2]; }\n", 42);
  run_ir_case("arr_2d",
              "int main(void) { int m[2][3]; int i, j; for (i = 0; i < 2; i = i + 1) for (j = 0; j < 3; j = j + 1) m[i][j] = i * 10 + j; return m[1][2]; }\n", 12);
  run_ir_case("arr_3d_sum",
              "int main(void) { int t[2][2][2]; int i, j, k; int s = 0; for (i = 0; i < 2; i = i + 1) for (j = 0; j < 2; j = j + 1) for (k = 0; k < 2; k = k + 1) { t[i][j][k] = i * 100 + j * 10 + k; s = s + t[i][j][k]; } return s; }\n", 188);
  run_ir_case("arr_as_ptr",
              "int sum(int *p, int n) { int s = 0; int i; for (i = 0; i < n; i = i + 1) s = s + p[i]; return s; } int main(void) { int arr[5]; arr[0] = 1; arr[1] = 2; arr[2] = 3; arr[3] = 4; arr[4] = 5; return sum(arr, 5); }\n", 15);
  run_ir_case("arr_bubble",
              "int main(void) { int a[5]; a[0] = 5; a[1] = 3; a[2] = 1; a[3] = 4; a[4] = 2; int i, j; for (i = 0; i < 4; i = i + 1) { for (j = 0; j < 4 - i; j = j + 1) { if (a[j] > a[j+1]) { int t = a[j]; a[j] = a[j+1]; a[j+1] = t; } } } return a[0] * 10000 + a[1] * 1000 + a[2] * 100 + a[3] * 10 + a[4]; }\n", 57);

  /* Phase 4d-1: 構造体メンバアクセス */
  run_ir_case("struct_basic",
              "struct S { int a; int b; }; int main(void) { struct S s; s.a = 10; s.b = 20; return s.a + s.b; }\n", 30);
  run_ir_case("struct_three_members",
              "struct P { int x; int y; int z; }; int main(void) { struct P p; p.x = 100; p.y = 20; p.z = 3; return p.x + p.y + p.z; }\n", 123);
  run_ir_case("struct_ptr_arrow",
              "struct S { int a; int b; }; int sum(struct S *p) { return p->a + p->b; } int main(void) { struct S s; s.a = 12; s.b = 30; return sum(&s); }\n", 42);
  run_ir_case("struct_ptr_set",
              "struct S { int a; int b; }; void set(struct S *p, int x, int y) { p->a = x; p->b = y; } int main(void) { struct S s; set(&s, 7, 35); return s.a + s.b; }\n", 42);
  run_ir_case("struct_member_array",
              "struct Vec { int data[3]; }; int main(void) { struct Vec v; v.data[0] = 5; v.data[1] = 7; v.data[2] = 30; return v.data[0] + v.data[1] + v.data[2]; }\n", 42);
  run_ir_case("struct_addr_member_ptr_walk",
              "struct S { int a; int b; }; int main(void) { struct S s; s.a = 10; s.b = 20; int *p = &s.a; return p[0] + p[1]; }\n", 30);

  /* Phase 4d-2: struct 値代入と引数渡し */
  run_ir_case("struct_value_assign_8B",
              "struct S { int a; int b; }; int main(void) { struct S s1; s1.a = 7; s1.b = 35; struct S s2; s2 = s1; return s2.a + s2.b; }\n", 42);
  run_ir_case("struct_value_assign_12B",
              "struct P { int x; int y; int z; }; int main(void) { struct P a; a.x = 1; a.y = 2; a.z = 3; struct P b; b = a; a.x = 99; return b.x + b.y + b.z; }\n", 6);
  run_ir_case("struct_arg_8B",
              "struct S { int a; int b; }; int sum(struct S s) { return s.a + s.b; } int main(void) { struct S s; s.a = 20; s.b = 22; return sum(s); }\n", 42);
  run_ir_case("struct_arg_12B",
              "struct P { int a; int b; int c; }; int sum(struct P p) { return p.a + p.b + p.c; } int main(void) { struct P p; p.a = 10; p.b = 12; p.c = 20; return sum(p); }\n", 42);
  run_ir_case("struct_arg_16B",
              "struct Q { int a; int b; int c; int d; }; int sum(struct Q q) { return q.a + q.b + q.c + q.d; } int main(void) { struct Q q; q.a = 10; q.b = 10; q.c = 10; q.d = 12; return sum(q); }\n", 42);
  run_ir_case("struct_arg_24B",
              "struct R { int a; int b; int c; int d; int e; int f; }; int sum(struct R r) { return r.a + r.b + r.c + r.d + r.e + r.f; } int main(void) { struct R r; r.a = 7; r.b = 7; r.c = 7; r.d = 7; r.e = 7; r.f = 7; return sum(r); }\n", 42);

  /* Phase 4d-3: struct 戻り値 */
  run_ir_case("struct_ret_8B",
              "struct S { int a; int b; }; struct S make(int x, int y) { struct S s; s.a = x; s.b = y; return s; } int main(void) { struct S s = make(15, 27); return s.a + s.b; }\n", 42);
  run_ir_case("struct_ret_16B",
              "struct Q { int a; int b; int c; int d; }; struct Q make(void) { struct Q q; q.a = 10; q.b = 10; q.c = 10; q.d = 12; return q; } int main(void) { struct Q q = make(); return q.a + q.b + q.c + q.d; }\n", 42);
  run_ir_case("struct_ret_24B",
              "struct R { int a; int b; int c; int d; int e; int f; }; struct R make(void) { struct R r; r.a = 7; r.b = 7; r.c = 7; r.d = 7; r.e = 7; r.f = 7; return r; } int main(void) { struct R r = make(); return r.a + r.b + r.c + r.d + r.e + r.f; }\n", 42);
  run_ir_case("struct_ret_with_args",
              "struct V { int x; int y; int z; }; struct V make(int a, int b, int c) { struct V v; v.x = a; v.y = b; v.z = c; return v; } int main(void) { struct V v = make(10, 13, 19); return v.x + v.y + v.z; }\n", 42);

  /* Phase 7b: bitfield */
  run_ir_case("bitfield_basic",
              "int main(void) { struct S { unsigned int a:3; unsigned int b:5; }; struct S s; s.a = 5; s.b = 10; return s.a; }\n", 5);
  run_ir_case("bitfield_unsigned_wrap",
              "int main(void) { struct S { unsigned int f:3; }; struct S s; s.f = 9; return s.f; }\n", 1);
  run_ir_case("bitfield_read_b",
              "int main(void) { struct S { unsigned int a:3; unsigned int b:5; }; struct S s; s.a = 5; s.b = 10; return s.b; }\n", 10);

  /* Phase 7c: switch */
  run_ir_case("switch_basic",
              "int main(void) { int x = 2; switch (x) { case 1: return 10; case 2: return 20; case 3: return 30; default: return 99; } }\n", 20);
  run_ir_case("switch_default",
              "int main(void) { int x = 7; switch (x) { case 1: return 10; case 2: return 20; default: return 99; } }\n", 99);
  run_ir_case("switch_break",
              "int main(void) { int x = 1; int r = 0; switch (x) { case 1: r = 10; break; case 2: r = 20; break; default: r = 99; } return r; }\n", 10);
  run_ir_case("switch_fallthrough",
              "int main(void) { int x = 1; int r = 0; switch (x) { case 1: r = r + 1; case 2: r = r + 2; case 3: r = r + 3; break; default: r = 99; } return r; }\n", 6);
  run_ir_case("switch_no_default_no_match",
              "int main(void) { int x = 7; int r = 5; switch (x) { case 1: r = 10; break; case 2: r = 20; break; } return r; }\n", 5);

  /* Phase 7d: float/double 基本 (ローカル変数 + 算術 + 比較 + 変換) */
  run_ir_case("float_basic",
              "int main(void) { float x = 1.5; float y = 2.5; return (int)(x + y); }\n", 4);
  run_ir_case("double_mul",
              "int main(void) { double x = 1.5; double y = 2.75; return (int)(x * y); }\n", 4);
  run_ir_case("double_cmp",
              "int main(void) { double x = 3.5; if (x > 3.0) return 7; return 0; }\n", 7);
  run_ir_case("int_double_mix",
              "int main(void) { int i = 3; double x = 2.5; return (int)(i + x); }\n", 5);
  run_ir_case("double_sub_div",
              "int main(void) { double a = 10.0; double b = 3.0; return (int)(a / b - 1.0); }\n", 2);

  /* Phase 7d-2: float 関数引数 / 戻り値 */
  run_ir_case("fp_func_square",
              "double square(double x) { return x * x; } int main(void) { double r = square(3.0); return (int)r; }\n", 9);
  run_ir_case("fp_func_multi_arg",
              "double mix(double a, double b, double c) { return a * b + c; } int main(void) { return (int)mix(2.0, 3.5, 1.0); }\n", 8);
  run_ir_case("fp_func_mix_int_double",
              "double pow2(int n, double base) { double r = 1.0; int i; for (i = 0; i < n; i = i + 1) r = r * base; return r; } int main(void) { return (int)pow2(3, 2.5); }\n", 15);
  run_ir_case("fp_func_div_chain",
              "double half(double x) { return x / 2.0; } int main(void) { return (int)(half(8.0) + half(4.0)); }\n", 6);

  /* Phase 7e: variadic (caller + callee) */
  run_ir_case("variadic_sum_int",
              "#include <stdarg.h>\nint sum(int n, ...) { va_list ap; va_start(ap, n); int s = 0; int i; for (i = 0; i < n; i = i + 1) s = s + va_arg(ap, int); va_end(ap); return s; }\nint main(void) { return sum(3, 10, 20, 12); }\n", 42);
  run_ir_case("variadic_avg_double",
              "#include <stdarg.h>\nint avg(int n, ...) { va_list ap; va_start(ap, n); double s = 0.0; int i; for (i = 0; i < n; i = i + 1) s = s + va_arg(ap, double); va_end(ap); return (int)(s / n); }\nint main(void) { return avg(3, 1.0, 2.0, 6.0); }\n", 3);
  run_ir_case("variadic_va_copy",
              "#include <stdarg.h>\nint twice(int n, ...) { va_list ap, cp; va_start(ap, n); va_copy(cp, ap); int s = 0, t = 0; int i; for (i = 0; i < n; i = i + 1) s = s + va_arg(ap, int); for (i = 0; i < n; i = i + 1) t = t + va_arg(cp, int); va_end(ap); va_end(cp); return s + t; }\nint main(void) { return twice(3, 10, 20, 30); }\n", 120);

  /* Phase 7f: 文字列リテラル + グローバル変数 + ローカル配列初期化子 */
  run_ir_case("printf_format",
              "#include <stdio.h>\nint main(void) { printf(\"hello %d\\n\", 42); return 0; }\n", 0);
  run_ir_case("global_scalar_read",
              "int x = 42;\nint main(void) { return x; }\n", 42);
  run_ir_case("global_scalar_write",
              "int g = 0;\nint main(void) { g = 99; return g; }\n", 99);
  run_ir_case("global_array_read",
              "int g[3] = {1, 5, 36};\nint main(void) { return g[0] + g[1] + g[2]; }\n", 42);
  run_ir_case("local_array_init",
              "int main(void) { int arr[3] = {10, 20, 12}; return arr[0] + arr[1] + arr[2]; }\n", 42);
  run_ir_case("string_literal_arg",
              "#include <stdio.h>\nint main(void) { char *s = \"world\"; printf(\"hello %s, %d\\n\", s, 42); return 0; }\n", 0);

  if (failures > 0) {
    fprintf(stderr, "IR Phase 2 E2E: %d/%d failed\n", failures, total);
    return 1;
  }
  printf("OK: All IR Phase 2 E2E tests passed! (%d/%d)\n", total, total);
  return 0;
}
