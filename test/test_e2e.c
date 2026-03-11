#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// コンパイル → アセンブル → 実行 → 終了コード取得
static int compile_and_run(const char *input) {
  // ag_c をパイプで呼び出し（シェルのクォート問題を回避）
  FILE *fp = fopen("build/tmp_e2e.s", "w");
  if (!fp) { fprintf(stderr, "  Cannot open tmp file\n"); return -1; }
  fclose(fp);

  int pipefd[2];
  pipe(pipefd);
  pid_t pid = fork();
  if (pid == 0) {
    // 子プロセス: stdout を .s ファイルにリダイレクト
    freopen("build/tmp_e2e.s", "w", stdout);
    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    _exit(1);
  }
  close(pipefd[0]); close(pipefd[1]);
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "  Compile failed for: %s\n", input);
    return -1;
  }

  int ret = system("clang -o build/tmp_e2e build/tmp_e2e.s 2>&1");
  if (ret != 0) {
    fprintf(stderr, "  Assemble failed for: %s\n", input);
    return -1;
  }

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
    printf("  OK: => %d\n", actual);
  } else {
    fprintf(stderr, "  FAIL: => expected %d, got %d\n  input: %s\n", expected,
            actual, input);
    exit(1);
  }
}

static float compile_and_run_float(const char *input, int is_double) {
  char buf[4096];
  strcpy(buf, input);
  char *m = strstr(buf, "main");
  if (m) memcpy(m, "ag_m", 4);

  FILE *fp = fopen("build/tmp_e2e.s", "w");
  if (!fp) { fprintf(stderr, "  Cannot open tmp file\n"); return -1; }
  fclose(fp);

  int pipefd[2]; pipe(pipefd);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("build/tmp_e2e.s", "w", stdout);
    execl("./build/ag_c", "./build/ag_c", buf, (char *)NULL);
    _exit(1);
  }
  close(pipefd[0]); close(pipefd[1]);
  int status; waitpid(pid, &status, 0);

  fp = fopen("build/driver.c", "w");
  if (is_double) {
    fprintf(fp, "#include <stdio.h>\nextern double ag_m();\nint main() { printf(\"%%.6lf\\n\", ag_m()); return 0; }\n");
  } else {
    fprintf(fp, "#include <stdio.h>\nextern float ag_m();\nint main() { printf(\"%%.6f\\n\", ag_m()); return 0; }\n");
  }
  fclose(fp);

  int ret = system("clang -o build/tmp_e2e build/tmp_e2e.s build/driver.c 2>&1");
  if (ret != 0) { fprintf(stderr, "  Assemble failed for float\n"); exit(1); }

  FILE *out = popen("./build/tmp_e2e", "r");
  if (is_double) {
    double d; fscanf(out, "%lf", &d); pclose(out); return d;
  } else {
    float f; fscanf(out, "%f", &f); pclose(out); return f;
  }
}

static void assert_result_float(float expected, const char *input) {
  test_count++;
  float actual = compile_and_run_float(input, 0);
  if (actual > expected - 0.001 && actual < expected + 0.001) {
    pass_count++;
    printf("  OK: => %.2f\n", actual);
  } else {
    fprintf(stderr, "  FAIL: => expected %.2f, got %.2f\n  input: %s\n", expected, actual, input);
    exit(1);
  }
}

static void assert_result_double(double expected, const char *input) {
  test_count++;
  double actual = compile_and_run_float(input, 1);
  if (actual > expected - 0.001 && actual < expected + 0.001) {
    pass_count++;
    printf("  OK: => %.2f\n", actual);
  } else {
    fprintf(stderr, "  FAIL: => expected %.2f, got %.2f\n  input: %s\n", expected, actual, input);
    exit(1);
  }
}

// --- テストケース ---
// 全テストが main() { ... } の関数定義形式

static void test_integer() {
  printf("test_integer...\n");
  assert_result(0, "main() { 0; }");
  assert_result(42, "main() { 42; }");
}

static void test_arithmetic() {
  printf("test_arithmetic...\n");
  assert_result(21, "main() { 5+20-4; }");
  assert_result(41, "main() { 12 + 34 - 5 ; }");
  assert_result(47, "main() { 5+6*7; }");
  assert_result(15, "main() { 5*(9-6); }");
  assert_result(4, "main() { (3+5)/2; }");
}

static void test_comparison() {
  printf("test_comparison...\n");
  assert_result(1, "main() { 0==0; }");
  assert_result(0, "main() { 42==0; }");
  assert_result(1, "main() { 0!=1; }");
  assert_result(0, "main() { 42!=42; }");
  assert_result(1, "main() { 0<1; }");
  assert_result(0, "main() { 1<1; }");
  assert_result(0, "main() { 2<1; }");
  assert_result(1, "main() { 0<=1; }");
  assert_result(1, "main() { 1<=1; }");
  assert_result(0, "main() { 2<=1; }");
  assert_result(1, "main() { 1>0; }");
  assert_result(0, "main() { 1>1; }");
  assert_result(0, "main() { 1>2; }");
  assert_result(1, "main() { 1>=0; }");
  assert_result(1, "main() { 1>=1; }");
  assert_result(0, "main() { 1>=2; }");
}

static void test_local_variables() {
  printf("test_local_variables...\n");
  assert_result(3, "main() { a=3; a; }");
  assert_result(14, "main() { a=3; b=5*6-8; a+b/2; }");
  assert_result(6, "main() { a=1; b=2; c=3; a+b+c; }");
  assert_result(10, "main() { a=5; a*2; }");
  assert_result(1, "main() { a=1; b=a; b; }");
}

static void test_if_else() {
  printf("test_if_else...\n");
  assert_result(3, "main() { a=3; if (a==3) a; else 0; }");
  assert_result(0, "main() { a=3; if (a==5) a; else 0; }");
  assert_result(5, "main() { a=3; if (a==3) 5; else 10; }");
  assert_result(10, "main() { a=3; if (a!=3) 5; else 10; }");
  assert_result(2, "main() { if (1) 2; else 3; }");
  assert_result(3, "main() { if (0) 2; else 3; }");
  assert_result(42, "main() { if (1) 42; }");
}

static void test_while() {
  printf("test_while...\n");
  assert_result(10, "main() { a=0; while (a<10) a=a+1; a; }");
  assert_result(0, "main() { a=0; while (0) a=a+1; a; }");
}

static void test_for() {
  printf("test_for...\n");
  assert_result(55, "main() { a=0; b=0; for (a=1; a<=10; a=a+1) b=b+a; b; }");
  assert_result(10, "main() { a=0; for (a=0; a<10; a=a+1) a; a; }");
}

static void test_return() {
  printf("test_return...\n");
  assert_result(42, "main() { return 42; }");
  assert_result(5, "main() { return 2+3; }");
  assert_result(10, "main() { a=10; return a; }");
  assert_result(3, "main() { a=1; b=2; return a+b; }");
  assert_result(1, "main() { if (1) return 1; else return 2; }");
  assert_result(10, "main() { a=0; while (a<10) a=a+1; return a; }");
}

static void test_block() {
  printf("test_block...\n");
  assert_result(3, "main() { { 1; 2; 3; } }");
  assert_result(6, "main() { a=1; b=2; c=3; { a+b+c; } }");
  assert_result(55, "main() { a=0; b=0; for (a=1; a<=10; a=a+1) { b=b+a; } return b; }");
  assert_result(10, "main() { a=0; while (a<10) { a=a+1; } return a; }");
  assert_result(5, "main() { if (1) { a=2; b=3; a+b; } else { 0; } }");
}

static void test_funcall() {
  printf("test_funcall...\n");
  // 引数なし関数
  assert_result(42, "fortytwo() { return 42; } main() { return fortytwo(); }");
  // 引数あり関数
  assert_result(7, "add(a, b) { return a+b; } main() { return add(3, 4); }");
  assert_result(10, "twice(a) { return a*2; } main() { return twice(5); }");
  // 複数関数の組み合わせ
  assert_result(21, "add(a, b) { return a+b; } mul(a, b) { return a*b; } main() { return add(mul(3, 4), mul(3, 3)); }");
  // 再帰（フィボナッチ的なもの — ただし深い再帰は避ける）
  assert_result(120, "fact(n) { if (n<=1) return 1; return n * fact(n-1); } main() { return fact(5); }");
}

static void test_multichar_var() {
  printf("test_multichar_var...\n");
  assert_result(3, "main() { foo=3; return foo; }");
  assert_result(5, "main() { hello=2; world=3; return hello+world; }");
  assert_result(15, "main() { x1=5; x2=10; return x1+x2; }");
  assert_result(10, "add(lhs, rhs) { return lhs+rhs; } main() { return add(3, 7); }");
  assert_result(6, "main() { count=0; for (i=1; i<=3; i=i+1) count=count+i; return count; }");
}

static void test_type_decl() {
  printf("test_type_decl...\n");
  // int 付き関数定義
  assert_result(42, "int main() { return 42; }");
  // int 付き変数宣言
  assert_result(3, "int main() { int x = 3; return x; }");
  assert_result(7, "int main() { int a = 3; int b = 4; return a+b; }");
  // int 付き仮引数
  assert_result(10, "int add(int a, int b) { return a+b; } int main() { return add(3, 7); }");
  // 初期化なし変数宣言
  assert_result(5, "int main() { int x; x = 5; return x; }");
  // for ループ内で int 宣言 (初期化式の前)
  assert_result(55, "int main() { int sum = 0; int i; for (i=1; i<=10; i=i+1) sum=sum+i; return sum; }");
  // char 型
  assert_result(65, "int main() { char c = 65; return c; }");
  // void 型関数
  assert_result(42, "void noop() { return 42; } int main() { return noop(); }");
  // short / long 型
  assert_result(10, "int main() { short s = 10; return s; }");
  assert_result(99, "long calc(long x) { return x+1; } int main() { return calc(98); }");
  // short配列（2バイトアクセス）
  assert_result(30, "int main() { short arr[3]; arr[0]=10; arr[1]=20; arr[2]=30; return arr[2]; }");
  assert_result(60, "int main() { short arr[3]; arr[0]=10; arr[1]=20; arr[2]=30; return arr[0]+arr[1]+arr[2]; }");
  assert_result(42, "int main() { short a = 42; return a; }");
  // float / double 型（FPU命令）
  assert_result_float(7.0f, "float main() { float f = 7; return f; }");
  assert_result_float(7.34f, "float main() { float f = 3.14; float g = 4.2; return f + g; }");
  assert_result_float(2.3f, "float main() { float f = 5.5; float g = 3.2; return f - g; }");
  assert_result_float(15.0f, "float main() { float f = 6.0f; float g = 2.5f; return f * g; }");
  assert_result_float(3.5f, "float main() { float f = 10.5F; float g = 3.0F; return f / g; }");
  // double
  assert_result_double(3.99, "double main() { double d = 3.99; return d; }");
  assert_result_double(7.3, "double main() { double a = 3.1; double b = 4.2; return a + b; }");
  assert_result_double(15.0, "double main() { double a = 5.0; double b = 3.0; return a * b; }");
  assert_result_double(5.0, "double main() { double a = 15.0; double b = 3.0; return a / b; }");
}

static void test_pointer() {
  printf("test_pointer...\n");
  // アドレス取得と間接参照
  assert_result(5, "int main() { int x = 5; int *p = &x; return *p; }");
  // ポインタ経由の代入
  assert_result(10, "int main() { int x = 5; int *p = &x; *p = 10; return x; }");
}

static void test_array() {
  printf("test_array...\n");
  // 配列宣言と添字アクセス
  assert_result(3, "int main() { int arr[3]; arr[0]=1; arr[1]=2; arr[2]=3; return arr[2]; }");
  assert_result(6, "int main() { int arr[3]; arr[0]=1; arr[1]=2; arr[2]=3; return arr[0]+arr[1]+arr[2]; }");
  // 配列とループ
  assert_result(55, "int main() { int arr[10]; int i; for(i=0; i<10; i=i+1) arr[i]=i+1; int sum=0; for(i=0; i<10; i=i+1) sum=sum+arr[i]; return sum; }");
}

static void test_string() {
  printf("test_string...\n");
  // 文字列の先頭文字を間接参照
  assert_result(65, "int main() { char *s = \"AB\"; return *s; }");
  // 文字列の添字アクセス（charは1バイト）
  assert_result(66, "int main() { char *s = \"AB\"; return s[1]; }");
  // 空文字列のNUL終端
  assert_result(0, "int main() { char *s = \"\"; return *s; }");
  // 文字リテラル
  assert_result(65, "int main() { return 'A'; }");
  assert_result(10, "int main() { return '\\n'; }");
  assert_result(0, "int main() { return '\\0'; }");
  // char配列の1バイトアクセス
  assert_result(3, "int main() { char buf[3]; buf[0]=1; buf[1]=2; buf[2]=3; return buf[2]; }");
  assert_result(6, "int main() { char buf[3]; buf[0]=1; buf[1]=2; buf[2]=3; return buf[0]+buf[1]+buf[2]; }");
  // char変数の1バイトストア/ロード
  assert_result(42, "int main() { char c = 42; return c; }");
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
  test_block();
  test_funcall();
  test_multichar_var();
  test_type_decl();
  test_pointer();
  test_array();
  test_string();

  printf("OK: All %d E2E tests passed! (%d/%d)\n", test_count, pass_count,
         test_count);
  return 0;
}
