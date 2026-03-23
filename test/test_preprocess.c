#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  int expected;
  const char *input;
} success_case_t;

static const success_case_t success_cases[] = {
    {42, "#include \"build/test_inc.h\"\nint main() { return inc_func(); }"},
    {42, "#include \"build/./dot_inc.h\"\nint main() { return dot_inc_func(); }"},
    {42, "#include \"build//slash_inc.h\"\nint main() { return slash_inc_func(); }"},
    {42, "#define FOO 42\nint main() { return FOO; }"},
    {10, "#define A 5\n#define B A\nint main() { return A + B; }"},
    {15, "#define PLUS_FIVE + 5\nint main() { return 10 PLUS_FIVE; }"},
    {42, "int main() { int A = 42;\n#define A A\n return A; }"},
    {42, "#define FOO 10\n#undef FOO\nint main() { int FOO = 42;\nreturn FOO; }"},
    {10, "#define FOO\n#ifdef FOO\nint main() { return 10; }\n#else\nint main() { return 20; }\n#endif"},
    {20, "#undef FOO\n#ifdef FOO\nint main() { return 10; }\n#else\nint main() { return 20; }\n#endif"},
    {10, "#ifndef FOO\nint main() { return 10; }\n#endif"},
    {5, "#if 1\nint main() { return 5; }\n#endif"},
    {5, "#if 0\nint main() { return 10; }\n#else\nint main() { return 5; }\n#endif"},
    {5, "#define FOO 1\n#if FOO\nint main() { return 5; }\n#endif"},
    {5, "#if defined(FOO)\nint main(){return 10;}\n#elif !defined(FOO)\nint main(){return 5;}\n#endif"},
    {42, "#if 1 + 2 == 3\nint main() { return 42; }\n#else\nint main() { return 0; }\n#endif"},
    {42, "#define X 5\n#if X * 2 > 8 && X < 10\nint main() { return 42; }\n#endif"},
    {42, "#define ADD(a, b) (a + b)\nint main() { return ADD(20, 22); }"},
    {10, "#define TWICE(x) (x * 2)\nint main() { return TWICE(5); }"},
    {25, "#define SQUARE(x) (x * x)\nint main() { return SQUARE(5); }"},
    {12, "#define ADD(x, y) (x+y)\n#define SUB(x, y) (x-y)\nint main() { return ADD(SUB(10, 2), 4); }"},
    {0, "#if 0\n#error \"This should not be evaluated\"\n#endif\nint main() { return 0; }"},
    {0, "#define STR(x) #x\nint main() { char *s = STR(hello world); if (s[0] == 'h') if (s[6] == 'w') return 0; return 1; }"},
    {42, "#define PASTE(a, b) a ## b\nint main() { int var123 = 42; return PASTE(var, 123); }"},
    // 定義済みマクロ
    {1,  "int main() { return __STDC__; }"},
    {42, "#if __STDC_VERSION__ >= 201112L\nint main() { return 42; }\n#else\nint main() { return 0; }\n#endif"},
    {1,  "int main() { return __LINE__; }"},
    {42, "int main() { char *f = __FILE__; return f[0] ? 42 : 0; }"},
    {42, "int main() { char *d = __DATE__; return d[0] ? 42 : 0; }"},
    {42, "int main() { char *t = __TIME__; return t[0] ? 42 : 0; }"},
    // #pragma once
    {42, "#include \"build/pragma_once.h\"\n#include \"build/pragma_once.h\"\nint main() { return once_func(); }"},
    {42, "#include \"build/pragma_once_rel.h\"\n#include \"./build/pragma_once_rel.h\"\nint main() { return once_rel_func(); }"},
    {42, "#include \"build/guard_a.h\"\nint main() { return guard_func(); }"},
    // #line ディレクティブ
    {99, "#line 99\nint main() { return __LINE__; }"},
    {42, "#line 41\nint x = 0;\nint main() { return __LINE__; }"},
    {1,  "#line 1 \"myfile.c\"\nint main() { char *f = __FILE__; return f[0] == 'm' ? 1 : 0; }"},
    // 標準ヘッダ
    {42, "#include <stdint.h>\nint main() { int32_t x = 42; return x; }"},
    {42, "#include <stdbool.h>\nint main() { _Bool b = 1; return b ? 42 : 0; }"},
    {42, "#include <stddef.h>\nint main() { size_t x = 42; return (int)x; }"},
    // 意地悪テスト: ネストした#if
    {42, "#if 1\n#if 1\n#if 1\nint main() { return 42; }\n#endif\n#endif\n#endif"},
    {42, "#if 1\n#if 0\nint main() { return 0; }\n#else\nint main() { return 42; }\n#endif\n#endif"},
    {10, "#if 0\n#if 1\nint main() { return 0; }\n#endif\n#else\nint main() { return 10; }\n#endif"},
    // 意地悪テスト: #elif チェーン
    {30, "#if 0\nint main() { return 10; }\n#elif 0\nint main() { return 20; }\n#elif 1\nint main() { return 30; }\n#else\nint main() { return 40; }\n#endif"},
    // 意地悪テスト: マクロの引数に括弧を含む
    {42, "#define ADD(a, b) (a + b)\nint main() { return ADD((20+2), (18+2)); }"},
    // 意地悪テスト: マクロの引数に別のマクロ
    {42, "#define X 20\n#define Y 22\n#define ADD(a, b) (a + b)\nint main() { return ADD(X, Y); }"},
    // 意地悪テスト: マクロ展開結果がさらにマクロを含む
    {42, "#define A B\n#define B 42\nint main() { return A; }"},
    // 意地悪テスト: 空マクロ本体
    {0, "#define EMPTY\nint main() { EMPTY return 0; }"},
    // 意地悪テスト: #define で括弧なし引数風の展開
    {42, "#define FOO (42)\nint main() { return FOO; }"},
    // 意地悪テスト: #if の複雑な式
    {42, "#if (1 + 2) * 3 == 9\nint main() { return 42; }\n#else\nint main() { return 0; }\n#endif"},
    {42, "#if 1 && (0 || 1)\nint main() { return 42; }\n#else\nint main() { return 0; }\n#endif"},
    {42, "#if !0 && !0\nint main() { return 42; }\n#else\nint main() { return 0; }\n#endif"},
    {42,
     "#if "
     "1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+"
     "1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+"
     "1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+"
     "1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1 == 64\n"
     "int main() { return 42; }\n"
     "#else\n"
     "int main() { return 0; }\n"
     "#endif"},
    // 意地悪テスト: #ifdef/#ifndef 連鎖
    {42, "#define X\n#ifdef X\n#ifndef Y\nint main() { return 42; }\n#endif\n#endif"},
    // 意地悪テスト: #undef してから #ifndef
    {42, "#define FOO\n#undef FOO\n#ifndef FOO\nint main() { return 42; }\n#endif"},
    // 意地悪テスト: マクロ内の文字列化 (#)
    {0, "#define CHECK(x) (x)\nint main() { return CHECK(0); }"},
    // 意地悪テスト: トークン貼り合わせ (##) で関数名を生成
    {42, "#define CALL(prefix) prefix##_func()\nint my_func() { return 42; }\nint main() { return CALL(my); }"},
    // 意地悪テスト: defined() の複雑な使用
    {42, "#define A\n#if defined(A) && !defined(B)\nint main() { return 42; }\n#endif"},
    // 意地悪テスト: __LINE__ が正確に増える
    {3, "int main() {\nreturn\n__LINE__;\n}"},
    // 意地悪テスト: マクロの再帰的展開防止（自己参照）
    {42, "#define X X\nint main() { int X = 42; return X; }"},
};

static const char *fail_cases[] = {
    "#else\nint main() { return 0; }\n",
    "#endif\nint main() { return 0; }\n",
    "#elif 1\nint main() { return 0; }\n",
    "#if 1\n#else\n#else\n#endif\nint main() { return 0; }\n",
    "#define\nint main() { return 0; }\n",
    "#undef\nint main() { return 0; }\n",
    "#if defined(\nint main() { return 0; }\n#endif\n",
    "#if defined(FOO\nint main() { return 0; }\n#endif\n",
    "#if defined()\nint main() { return 0; }\n#endif\n",
    "#define FOO(1) 1\nint main() { return FOO(1); }\n",
    "#define FOO(a, 1) 1\nint main() { return FOO(1); }\n",
    "#include <stdio.h\nint main() { return 0; }\n",
    "#include \"build/not_found.h\"\nint main() { return 0; }\n",
    "#include \"build/\\u202Eevil.h\"\nint main() { return 0; }\n",
    "#include \"build\\\\test_inc.h\"\nint main() { return 0; }\n",
    "#include \"C:/Windows/win.ini\"\nint main() { return 0; }\n",
    "#include \"/tmp/blocked_absolute_path.h\"\nint main() { return 0; }\n",
    "#include \"../README.md\"\nint main() { return 0; }\n",
    "#include \"build/\"\nint main() { return 0; }\n",
    "#include \".\"\nint main() { return 0; }\n",
    "#include <../README.md>\nint main() { return 0; }\n",
    "#include \"build/self_include.h\"\nint main() { return 0; }\n",
    "#include \"build/cycle_norm_a.h\"\nint main() { return 0; }\n",
    "#include \"build/cycle_a.h\"\nint main() { return 0; }\n",
    "#include \"./build/cycle_norm_a.h\"\nint main() { return 0; }\n",
    "#include \"build/depth_00.h\"\nint main() { return 0; }\n",
    "#line 0\nint main() { return 0; }\n",
    "#line 2147483648\nint main() { return 0; }\n",
    "#define FOO(x) x\nint main() { return FOO(1; }\n",
    "#define ADD(a, b) ((a) + (b))\nint main() { return ADD(1); }\n",
    "#define ADD(a, b) ((a) + (b))\nint main() { return ADD(1, 2, 3); }\n",
    "#define ADD(a, b) ((a) + (b))\nint main() { return ADD(1,); }\n",
    "#define BAD1(a) ##a\nint main() { return BAD1(42); }\n",
    "#define BAD2(a) a##\nint main() { return BAD2(42); }\n",
    "#define BAD3(a,b) a###b\nint main() { return BAD3(1,2); }\n",
    "#define BAD4(a) a##+\nint main() { return BAD4(1); }\n",
    "#if 1 /* unterminated\nint main() { return 0; }\n#endif\n",
    "#error \"forced\"\nint main() { return 0; }\n",
};

static int write_input_file(const char *path, const char *input) {
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  fputs(input, fp);
  fclose(fp);
  return 0;
}

static int run_ag_c_to_s(const char *src_path, const char *s_path) {
  pid_t pid = fork();
  if (pid == 0) {
    freopen(s_path, "w", stdout);
    execl("./build/ag_c", "./build/ag_c", src_path, (char *)NULL);
    _exit(1);
  }
  if (pid < 0) return -1;
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
  return 0;
}

static int copy_replace_main_symbol(const char *src_path, const char *dst_path, const char *sym) {
  FILE *in = fopen(src_path, "r");
  if (!in) return -1;
  FILE *out = fopen(dst_path, "w");
  if (!out) {
    fclose(in);
    return -1;
  }
  const char *from = "_main";
  size_t from_len = strlen(from);
  char *line = NULL;
  size_t cap = 0;
  while (getline(&line, &cap, in) != -1) {
    char *p = line;
    while (1) {
      char *hit = strstr(p, from);
      if (!hit) {
        fputs(p, out);
        break;
      }
      fwrite(p, 1, (size_t)(hit - p), out);
      fputs(sym, out);
      p = hit + from_len;
    }
  }
  free(line);
  fclose(in);
  fclose(out);
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
    for (size_t i = 0; i < ninputs; i++) argv[3 + i] = (char *)inputs[i];
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

static int run_binary_exit_code(const char *bin_path, int *exit_code) {
  pid_t pid = fork();
  if (pid == 0) {
    execl(bin_path, bin_path, (char *)NULL);
    _exit(1);
  }
  if (pid < 0) return -1;
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status)) return -1;
  *exit_code = WEXITSTATUS(status);
  return 0;
}

static int run_success_cases_batched(void) {
  static const size_t ncases = sizeof(success_cases) / sizeof(success_cases[0]);
  const char **clang_inputs = calloc(ncases + 1, sizeof(char *));
  char **owned_paths = calloc(ncases + 1, sizeof(char *));
  if (!clang_inputs || !owned_paths) {
    free(clang_inputs);
    free(owned_paths);
    return -1;
  }

  FILE *drv = fopen("build/tmp_cpp_driver.c", "w");
  if (!drv) {
    free(clang_inputs);
    free(owned_paths);
    return -1;
  }
  fprintf(drv, "#include <stdio.h>\nint main(void) { int failed = 0;\n");

  for (size_t i = 0; i < ncases; i++) {
    char src_path[256];
    char s_path[256];
    char rs_path[256];
    char sym[64];
    snprintf(src_path, sizeof(src_path), "build/tmp_cpp_case_%zu.c", i);
    snprintf(s_path, sizeof(s_path), "build/tmp_cpp_case_%zu.s", i);
    snprintf(rs_path, sizeof(rs_path), "build/tmp_cpp_case_%zu.renamed.s", i);
    snprintf(sym, sizeof(sym), "_pp_case_%zu_main", i);

    if (write_input_file(src_path, success_cases[i].input) != 0 ||
        run_ag_c_to_s(src_path, s_path) != 0 ||
        copy_replace_main_symbol(s_path, rs_path, sym) != 0) {
      fclose(drv);
      free(clang_inputs);
      free(owned_paths);
      return -1;
    }

    owned_paths[i] = strdup(rs_path);
    if (!owned_paths[i]) {
      fclose(drv);
      free(clang_inputs);
      free(owned_paths);
      return -1;
    }
    clang_inputs[i] = owned_paths[i];

    fprintf(drv, "  extern int pp_case_%zu_main(void);\n", i);
  }

  fprintf(drv, "\n");
  for (size_t i = 0; i < ncases; i++) {
    fprintf(drv,
            "  { int actual = (pp_case_%zu_main() & 255); if (actual != %d) { failed = 1; printf(\"FAIL case_%zu expected %d got %%d\\n\", actual); } }\n",
            i, success_cases[i].expected, i, success_cases[i].expected);
  }
  fprintf(drv, "  return failed;\n}\n");
  fclose(drv);

  owned_paths[ncases] = strdup("build/tmp_cpp_driver.c");
  if (!owned_paths[ncases]) {
    for (size_t i = 0; i < ncases; i++) free(owned_paths[i]);
    free(clang_inputs);
    free(owned_paths);
    return -1;
  }
  clang_inputs[ncases] = owned_paths[ncases];
  if (run_clang_build_many("build/tmp_cpp_runner", clang_inputs, ncases + 1) != 0) {
    for (size_t i = 0; i <= ncases; i++) free(owned_paths[i]);
    free(clang_inputs);
    free(owned_paths);
    return -1;
  }

  int ret = -1;
  if (run_binary_exit_code("build/tmp_cpp_runner", &ret) != 0) ret = -1;
  for (size_t i = 0; i <= ncases; i++) free(owned_paths[i]);
  free(clang_inputs);
  free(owned_paths);
  return ret;
}

static void expect_preprocess_fail(const char *input) {
  const char *src_path = "build/tmp_cpp_input_fail.c";
  if (write_input_file(src_path, input) != 0) {
    fprintf(stderr, "  FAIL: cannot create input file\n");
    exit(1);
  }

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    execl("./build/ag_c", "./build/ag_c", src_path, (char *)NULL);
    _exit(1);
  }
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) {
    fprintf(stderr, "  FAIL: expected preprocess error\n  input: %s\n", input);
    exit(1);
  }
}

static void expect_preprocess_fail_with_stderr_substr(const char *input, const char *needle) {
  const char *src_path = "build/tmp_cpp_input_fail_diag.c";
  const char *err_path = "build/tmp_cpp_input_fail_diag.err";
  if (write_input_file(src_path, input) != 0) {
    fprintf(stderr, "  FAIL: cannot create input file\n");
    exit(1);
  }

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen(err_path, "w", stderr);
    execl("./build/ag_c", "./build/ag_c", src_path, (char *)NULL);
    _exit(1);
  }
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) {
    fprintf(stderr, "  FAIL: expected preprocess error\n  input: %s\n", input);
    exit(1);
  }

  FILE *fp = fopen(err_path, "r");
  if (!fp) {
    fprintf(stderr, "  FAIL: cannot open captured stderr\n");
    exit(1);
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    fprintf(stderr, "  FAIL: cannot seek captured stderr\n");
    exit(1);
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    fprintf(stderr, "  FAIL: cannot read captured stderr size\n");
    exit(1);
  }
  rewind(fp);
  char *buf = calloc((size_t)sz + 1, 1);
  if (!buf) {
    fclose(fp);
    fprintf(stderr, "  FAIL: cannot allocate stderr buffer\n");
    exit(1);
  }
  if (sz > 0) {
    (void)fread(buf, 1, (size_t)sz, fp);
  }
  fclose(fp);

  if (!strstr(buf, needle)) {
    fprintf(stderr, "  FAIL: expected diagnostic substring not found: %s\n", needle);
    fprintf(stderr, "  stderr: %s\n", buf);
    free(buf);
    exit(1);
  }
  free(buf);
}

static void expect_macro_expansion_limit_fail(void) {
  const int levels = 15;
  size_t cap = 8192;
  char *input = calloc(cap, 1);
  if (!input) {
    fprintf(stderr, "  FAIL: cannot allocate macro expansion test input\n");
    exit(1);
  }
  size_t len = 0;

  int n = snprintf(input + len, cap - len, "#define X0 1\n");
  if (n < 0 || (size_t)n >= cap - len) {
    fprintf(stderr, "  FAIL: macro expansion input overflow\n");
    free(input);
    exit(1);
  }
  len += (size_t)n;

  for (int i = 1; i <= levels; i++) {
    n = snprintf(input + len, cap - len, "#define X%d (X%d + X%d)\n", i, i - 1, i - 1);
    if (n < 0 || (size_t)n >= cap - len) {
      fprintf(stderr, "  FAIL: macro expansion input overflow\n");
      free(input);
      exit(1);
    }
    len += (size_t)n;
  }

  n = snprintf(input + len, cap - len, "int main() { return X%d; }\n", levels);
  if (n < 0 || (size_t)n >= cap - len) {
    fprintf(stderr, "  FAIL: macro expansion input overflow\n");
    free(input);
    exit(1);
  }

  expect_preprocess_fail(input);
  free(input);
}

static void expect_macro_arg_nesting_limit_fail(void) {
  const int reps = 25050;
  size_t cap = (size_t)reps * 6 + 256;
  char *input = calloc(cap, 1);
  if (!input) {
    fprintf(stderr, "  FAIL: cannot allocate macro arg nesting input\n");
    exit(1);
  }
  size_t len = 0;
  int n = snprintf(input + len, cap - len,
                   "#define F0(x) x\n"
                   "#define F1(x) F0(x)\n"
                   "#define F2(x) F1(x)\n"
                   "int main() { return ");
  if (n < 0 || (size_t)n >= cap - len) {
    fprintf(stderr, "  FAIL: macro arg nesting input overflow\n");
    free(input);
    exit(1);
  }
  len += (size_t)n;
  for (int i = 0; i < reps; i++) {
    if (i > 0) input[len++] = '+';
    memcpy(input + len, "F2(0)", 5);
    len += 5;
  }
  n = snprintf(input + len, cap - len, "; }\n");
  if (n < 0 || (size_t)n >= cap - len) {
    fprintf(stderr, "  FAIL: macro arg nesting input overflow\n");
    free(input);
    exit(1);
  }

  expect_preprocess_fail(input);
  free(input);
}

int main(void) {
  printf("Running Preprocessor tests...\n");
  fflush(stdout);

  FILE *h = fopen("build/test_inc.h", "w");
  fprintf(h, "int inc_func() { return 42; }\n");
  fclose(h);
  FILE *hdot = fopen("build/dot_inc.h", "w");
  fprintf(hdot, "int dot_inc_func() { return 42; }\n");
  fclose(hdot);
  FILE *hslash = fopen("build/slash_inc.h", "w");
  fprintf(hslash, "int slash_inc_func() { return 42; }\n");
  fclose(hslash);
  FILE *honce = fopen("build/pragma_once.h", "w");
  fprintf(honce, "#pragma once\nint once_func() { return 42; }\n");
  fclose(honce);
  FILE *honce_rel = fopen("build/pragma_once_rel.h", "w");
  fprintf(honce_rel, "#pragma once\nint once_rel_func() { return 42; }\n");
  fclose(honce_rel);
  FILE *hguard_a = fopen("build/guard_a.h", "w");
  fprintf(hguard_a,
          "#ifndef GUARD_A_H\n"
          "#define GUARD_A_H\n"
          "#include \"build/guard_b.h\"\n"
          "int guard_func() { return 42; }\n"
          "#endif\n");
  fclose(hguard_a);
  FILE *hguard_b = fopen("build/guard_b.h", "w");
  fprintf(hguard_b,
          "#ifndef GUARD_B_H\n"
          "#define GUARD_B_H\n"
          "#include \"build/guard_a.h\"\n"
          "#endif\n");
  fclose(hguard_b);
  FILE *hself = fopen("build/self_include.h", "w");
  fprintf(hself, "#include \"build/self_include.h\"\n");
  fclose(hself);
  FILE *hcycle_norm_a = fopen("build/cycle_norm_a.h", "w");
  fprintf(hcycle_norm_a,
          "#pragma once\n"
          "#include \"build//cycle_norm_b.h\"\n"
          "int cycle_norm_func() { return 42; }\n");
  fclose(hcycle_norm_a);
  FILE *hcycle_norm_b = fopen("build/cycle_norm_b.h", "w");
  fprintf(hcycle_norm_b,
          "#pragma once\n"
          "#include \"build/cycle_norm_a.h\"\n");
  fclose(hcycle_norm_b);
  FILE *ha = fopen("build/cycle_a.h", "w");
  fprintf(ha, "#include \"build/cycle_b.h\"\n");
  fclose(ha);
  FILE *hb = fopen("build/cycle_b.h", "w");
  fprintf(hb, "#include \"build/cycle_a.h\"\n");
  fclose(hb);
  FILE *hcn_a = fopen("build/cycle_norm_a.h", "w");
  fprintf(hcn_a, "#include \"build//cycle_norm_b.h\"\n");
  fclose(hcn_a);
  FILE *hcn_b = fopen("build/cycle_norm_b.h", "w");
  fprintf(hcn_b, "#include \"./build/cycle_norm_a.h\"\n");
  fclose(hcn_b);
  for (int i = 0; i < 70; i++) {
    char path[64];
    snprintf(path, sizeof(path), "build/depth_%02d.h", i);
    FILE *hd = fopen(path, "w");
    if (!hd) {
      fprintf(stderr, "  FAIL: cannot create %s\n", path);
      return 1;
    }
    if (i + 1 < 70) {
      fprintf(hd, "#include \"build/depth_%02d.h\"\n", i + 1);
    } else {
      fprintf(hd, "int depth_leaf(void) { return 0; }\n");
    }
    fclose(hd);
  }

  int success_ret = run_success_cases_batched();
  if (success_ret != 0) {
    fprintf(stderr, "  FAIL: batched preprocess success cases failed\n");
    return 1;
  }

  for (size_t i = 0; i < sizeof(fail_cases) / sizeof(fail_cases[0]); i++) {
    expect_preprocess_fail(fail_cases[i]);
  }
  expect_preprocess_fail_with_stderr_substr("#line 2147483648\nint main() { return 0; }\n", "E1027");
  expect_macro_expansion_limit_fail();
  expect_macro_arg_nesting_limit_fail();

  printf("OK: Preprocessor tests passed!\n");
  return 0;
}
