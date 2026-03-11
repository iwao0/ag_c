#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int compile_and_run(const char *input) {
  FILE *fp = fopen("build/tmp_cpp.s", "w");
  if (!fp) { fprintf(stderr, "  Cannot open tmp file\n"); return -1; }
  fclose(fp);

  int pipefd[2];
  pipe(pipefd);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("build/tmp_cpp.s", "w", stdout);
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

int main() {
  printf("Running Preprocessor tests...\n");

  // Create a dummy header file
  FILE *h = fopen("build/test_inc.h", "w");
  fprintf(h, "int inc_func() { return 42; }\n");
  fclose(h);

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

  printf("OK: Preprocessor tests passed!\n");
  return 0;
}
