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

  /* 実行 */
  rc = system(bin_path);
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

  if (failures > 0) {
    fprintf(stderr, "IR Phase 2 E2E: %d/%d failed\n", failures, total);
    return 1;
  }
  printf("OK: All IR Phase 2 E2E tests passed! (%d/%d)\n", total, total);
  return 0;
}
