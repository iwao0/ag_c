#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int write_input_file(const char *path, const char *input) {
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  fputs(input, fp);
  fclose(fp);
  return 0;
}

static int compile_and_run(const char *input) {
  const char *src_path = "build/tmp_cpp_input.c";
  if (write_input_file(src_path, input) != 0) {
    fprintf(stderr, "  Cannot create input file\n");
    return -1;
  }

  FILE *fp = fopen("build/tmp_cpp.s", "w");
  if (!fp) { fprintf(stderr, "  Cannot open tmp file\n"); return -1; }
  fclose(fp);

  int pipefd[2];
  pipe(pipefd);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("build/tmp_cpp.s", "w", stdout);
    execl("./build/ag_c", "./build/ag_c", src_path, (char *)NULL);
    _exit(1);
  }
  close(pipefd[0]); close(pipefd[1]);
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "  Compile failed for: %s\n", input);
    return -1;
  }

  int ret = system("clang -o build/tmp_cpp build/tmp_cpp.s 2>&1");
  if (ret != 0) {
    fprintf(stderr, "  Assemble failed for: %s\n", input);
    return -1;
  }

  ret = system("./build/tmp_cpp");
  return WEXITSTATUS(ret);
}

static void assert_result(int expected, const char *input) {
  int actual = compile_and_run(input);
  if (actual == expected) {
    printf("  OK: %s => %d\n", input, actual);
  } else {
    fprintf(stderr, "  FAIL: => expected %d, got %d\n  input: %s\n", expected,
            actual, input);
    exit(1);
  }
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

int main() {
  printf("Running Preprocessor tests...\n");

  // Create a dummy header file
  FILE *h = fopen("build/test_inc.h", "w");
  fprintf(h, "int inc_func() { return 42; }\n");
  fclose(h);
  FILE *ha = fopen("build/cycle_a.h", "w");
  fprintf(ha, "#include \"build/cycle_b.h\"\n");
  fclose(ha);
  FILE *hb = fopen("build/cycle_b.h", "w");
  fprintf(hb, "#include \"build/cycle_a.h\"\n");
  fclose(hb);

  assert_result(42, "#include \"build/test_inc.h\"\nint main() { return inc_func(); }");

  // Tests for object-like macros
  assert_result(42, "#define FOO 42\nint main() { return FOO; }");
  assert_result(10, "#define A 5\n#define B A\nint main() { return A + B; }");
  assert_result(15, "#define PLUS_FIVE + 5\nint main() { return 10 PLUS_FIVE; }");
  
  // Test hideset (recursive macro expansion prevention)
  assert_result(42, "int main() { int A = 42;\n#define A A\n return A; }");

  // Test #undef
  assert_result(42, "#define FOO 10\n#undef FOO\nint main() { int FOO = 42;\nreturn FOO; }");

  // Conditional compilation
  assert_result(10, "#define FOO\n#ifdef FOO\nint main() { return 10; }\n#else\nint main() { return 20; }\n#endif");
  assert_result(20, "#undef FOO\n#ifdef FOO\nint main() { return 10; }\n#else\nint main() { return 20; }\n#endif");
  assert_result(10, "#ifndef FOO\nint main() { return 10; }\n#endif");
  
  assert_result(5, "#if 1\nint main() { return 5; }\n#endif");
  assert_result(5, "#if 0\nint main() { return 10; }\n#else\nint main() { return 5; }\n#endif");
  assert_result(5, "#define FOO 1\n#if FOO\nint main() { return 5; }\n#endif");
  assert_result(5, "#if defined(FOO)\nint main(){return 10;}\n#elif !defined(FOO)\nint main(){return 5;}\n#endif");
  
  // Test #if const expr
  assert_result(42, "#if 1 + 2 == 3\nint main() { return 42; }\n#else\nint main() { return 0; }\n#endif");
  assert_result(42, "#define X 5\n#if X * 2 > 8 && X < 10\nint main() { return 42; }\n#endif");

  // Function-like macros
  assert_result(42, "#define ADD(a, b) (a + b)\nint main() { return ADD(20, 22); }");
  assert_result(10, "#define TWICE(x) (x * 2)\nint main() { return TWICE(5); }");
  assert_result(25, "#define SQUARE(x) (x * x)\nint main() { return SQUARE(5); }");
  
  // Function-like macro with recursive expansion
  assert_result(12, "#define ADD(x, y) (x+y)\n#define SUB(x, y) (x-y)\nint main() { return ADD(SUB(10, 2), 4); }");
  
  // Test #error skipping (if it reaches #error in an active block, the test would fail, which is exactly why it should not execute here)
  assert_result(0, "#if 0\n#error \"This should not be evaluated\"\n#endif\nint main() { return 0; }");

  // Stringification test
  assert_result(0, "#define STR(x) #x\nint main() { char *s = STR(hello world); if (s[0] == 'h') if (s[6] == 'w') return 0; return 1; }");

  // Token pasting test
  assert_result(42, "#define PASTE(a, b) a ## b\nint main() { int var123 = 42; return PASTE(var, 123); }");

  // Invalid directives / malformed preprocessor usage
  expect_preprocess_fail("#else\nint main() { return 0; }\n");
  expect_preprocess_fail("#endif\nint main() { return 0; }\n");
  expect_preprocess_fail("#elif 1\nint main() { return 0; }\n");
  expect_preprocess_fail("#if 1\n#else\n#else\n#endif\nint main() { return 0; }\n");
  expect_preprocess_fail("#define\nint main() { return 0; }\n");
  expect_preprocess_fail("#undef\nint main() { return 0; }\n");
  expect_preprocess_fail("#if defined(\nint main() { return 0; }\n#endif\n");
  expect_preprocess_fail("#if defined(FOO\nint main() { return 0; }\n#endif\n");
  expect_preprocess_fail("#if defined()\nint main() { return 0; }\n#endif\n");
  expect_preprocess_fail("#define FOO(1) 1\nint main() { return FOO(1); }\n");
  expect_preprocess_fail("#define FOO(a, 1) 1\nint main() { return FOO(1); }\n");
  expect_preprocess_fail("#include <stdio.h\nint main() { return 0; }\n");
  expect_preprocess_fail("#include \"build/not_found.h\"\nint main() { return 0; }\n");
  expect_preprocess_fail("#include \"../README.md\"\nint main() { return 0; }\n");
  expect_preprocess_fail("#include \"build/cycle_a.h\"\nint main() { return 0; }\n");
  expect_preprocess_fail("#define FOO(x) x\nint main() { return FOO(1; }\n"); // ')' missing
  expect_preprocess_fail("#error \"forced\"\nint main() { return 0; }\n");

  printf("OK: Preprocessor tests passed!\n");
  return 0;
}
