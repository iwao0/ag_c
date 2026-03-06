#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// コンパイル → アセンブル → 実行 → 終了コード取得
static int compile_and_run(const char *input) {
  // 1. ag_c でアセンブリを生成
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "./build/ag_c '%s' > build/tmp_e2e.s 2>&1", input);
  int ret = system(cmd);
  if (ret != 0) {
    fprintf(stderr, "  Compile failed for: %s\n", input);
    return -1;
  }

  // 2. clang でアセンブル・リンク
  ret = system("clang -o build/tmp_e2e build/tmp_e2e.s 2>&1");
  if (ret != 0) {
    fprintf(stderr, "  Assemble failed for: %s\n", input);
    return -1;
  }

  // 3. 実行して終了コードを取得
  ret = system("./build/tmp_e2e");
  return WEXITSTATUS(ret);
}

static int test_count = 0;
static int pass_count = 0;

static void assert_result(int expected, const char *input) {
  test_count++;
  int actual = compile_and_run(input);
  if (actual == expected) {
    pass_count++;
    printf("  OK: \"%s\" => %d\n", input, actual);
  } else {
    fprintf(stderr, "  FAIL: \"%s\" => expected %d, got %d\n", input, expected,
            actual);
    exit(1);
  }
}

// --- テストケース ---

static void test_integer() {
  printf("test_integer...\n");
  assert_result(0, "0;");
  assert_result(42, "42;");
}

static void test_arithmetic() {
  printf("test_arithmetic...\n");
  assert_result(21, "5+20-4;");
  assert_result(41, " 12 + 34 - 5 ;");
  assert_result(47, "5+6*7;");
  assert_result(15, "5*(9-6);");
  assert_result(4, "(3+5)/2;");
}

static void test_comparison() {
  printf("test_comparison...\n");
  // ==, !=
  assert_result(1, "0==0;");
  assert_result(0, "42==0;");
  assert_result(1, "0!=1;");
  assert_result(0, "42!=42;");
  // <, <=
  assert_result(1, "0<1;");
  assert_result(0, "1<1;");
  assert_result(0, "2<1;");
  assert_result(1, "0<=1;");
  assert_result(1, "1<=1;");
  assert_result(0, "2<=1;");
  // >, >=
  assert_result(1, "1>0;");
  assert_result(0, "1>1;");
  assert_result(0, "1>2;");
  assert_result(1, "1>=0;");
  assert_result(1, "1>=1;");
  assert_result(0, "1>=2;");
}

static void test_local_variables() {
  printf("test_local_variables...\n");
  assert_result(3, "a=3; a;");
  assert_result(14, "a=3; b=5*6-8; a+b/2;");
  assert_result(6, "a=1; b=2; c=3; a+b+c;");
  assert_result(10, "a=5; a*2;");
  assert_result(1, "a=1; b=a; b;");
}

static void test_if_else() {
  printf("test_if_else...\n");
  assert_result(3, "a=3; if (a==3) a; else 0;");
  assert_result(0, "a=3; if (a==5) a; else 0;");
  assert_result(5, "a=3; if (a==3) 5; else 10;");
  assert_result(10, "a=3; if (a!=3) 5; else 10;");
  assert_result(2, "if (1) 2; else 3;");
  assert_result(3, "if (0) 2; else 3;");
  assert_result(42, "if (1) 42;");
}

static void test_while() {
  printf("test_while...\n");
  assert_result(10, "a=0; while (a<10) a=a+1; a;");
  assert_result(0, "a=0; while (0) a=a+1; a;");
}

static void test_for() {
  printf("test_for...\n");
  assert_result(55, "a=0; b=0; for (a=1; a<=10; a=a+1) b=b+a; b;");
  assert_result(10, "a=0; for (a=0; a<10; a=a+1) a; a;");
}

static void test_return() {
  printf("test_return...\n");
  assert_result(42, "return 42;");
  assert_result(5, "return 2+3;");
  assert_result(10, "a=10; return a;");
  assert_result(3, "a=1; b=2; return a+b;");
  assert_result(1, "if (1) return 1; else return 2;");
  assert_result(10, "a=0; while (a<10) a=a+1; return a;");
}

int main() {
  printf("Running E2E tests...\n");

  test_integer();
  test_arithmetic();
  test_comparison();
  test_local_variables();
  test_if_else();
  test_while();
  test_for();
  test_return();

  printf("OK: All %d E2E tests passed! (%d/%d)\n", test_count, pass_count,
         test_count);
  return 0;
}
