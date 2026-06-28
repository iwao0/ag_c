#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int write_file(const char *path, const char *src) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return -1;
  fwrite(src, 1, strlen(src), fp);
  fclose(fp);
  return 0;
}

static int slurp(const char *path, char *buf, size_t cap) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return -1;
  size_t n = fread(buf, 1, cap - 1, fp);
  fclose(fp);
  buf[n] = '\0';
  return 0;
}

static int command_available(const char *cmd) {
  char probe[256];
  snprintf(probe, sizeof(probe), "command -v %s > /dev/null 2>&1", cmd);
  return system(probe) == 0;
}

static int run_wabt_case(const char *name, int expected_ret) {
  if (!command_available("wat2wasm") || !command_available("wasm-validate") ||
      !command_available("wasm-interp")) {
    return 0;
  }
  char wat[256];
  char wasm[256];
  char log[256];
  snprintf(wat, sizeof(wat), "build/wasm32_%s.wat", name);
  snprintf(wasm, sizeof(wasm), "build/wasm32_%s.wasm", name);
  snprintf(log, sizeof(log), "build/wasm32_%s.interp.log", name);

  char cmd[768];
  snprintf(cmd, sizeof(cmd), "wat2wasm %s -o %s", wat, wasm);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: wat2wasm failed for %s\n", name);
    return 1;
  }
  snprintf(cmd, sizeof(cmd), "wasm-validate %s", wasm);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: wasm-validate failed for %s\n", name);
    return 1;
  }
  snprintf(cmd, sizeof(cmd), "wasm-interp %s --run-all-exports > %s", wasm, log);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: wasm-interp failed for %s\n", name);
    return 1;
  }
  char buf[8192];
  if (slurp(log, buf, sizeof(buf)) != 0) return 1;
  char expected[64];
  snprintf(expected, sizeof(expected), "main() => i32:%d", expected_ret);
  if (!strstr(buf, expected)) {
    fprintf(stderr, "FAIL: %s expected interp result '%s'\n", name, expected);
    return 1;
  }
  return 0;
}

static int run_case(const char *name, const char *src, const char **needles, int nneedles,
                    int expected_ret) {
  char in[256];
  char out[256];
  snprintf(in, sizeof(in), "build/wasm32_%s.c", name);
  snprintf(out, sizeof(out), "build/wasm32_%s.wat", name);
  if (write_file(in, src) != 0) {
    fprintf(stderr, "FAIL: write %s\n", in);
    return 1;
  }
  char cmd[768];
  snprintf(cmd, sizeof(cmd), "./build/ag_c_wasm %s > %s", in, out);
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "FAIL: ag_c_wasm failed for %s (rc=%d)\n", name, rc);
    return 1;
  }
  char buf[65536];
  if (slurp(out, buf, sizeof(buf)) != 0) {
    fprintf(stderr, "FAIL: read %s\n", out);
    return 1;
  }
  for (int i = 0; i < nneedles; i++) {
    if (!strstr(buf, needles[i])) {
      fprintf(stderr, "FAIL: %s missing '%s'\n", name, needles[i]);
      return 1;
    }
  }
  return run_wabt_case(name, expected_ret);
}

static int run_fail_case(const char *name, const char *src, const char *needle) {
  char in[256];
  char log[256];
  snprintf(in, sizeof(in), "build/wasm32_%s.c", name);
  snprintf(log, sizeof(log), "build/wasm32_%s.log", name);
  if (write_file(in, src) != 0) return 1;
  char cmd[768];
  snprintf(cmd, sizeof(cmd), "./build/ag_c_wasm %s > /dev/null 2> %s", in, log);
  int rc = system(cmd);
  if (rc == 0) {
    fprintf(stderr, "FAIL: expected ag_c_wasm failure for %s\n", name);
    return 1;
  }
  char buf[8192];
  if (slurp(log, buf, sizeof(buf)) != 0) return 1;
  if (!strstr(buf, needle)) {
    fprintf(stderr, "FAIL: %s missing diagnostic '%s'\n", name, needle);
    return 1;
  }
  return 0;
}

int main(void) {
  int failures = 0;
  const char *basic[] = {"(module", "(memory (export \"memory\") 1)", "(func $main", "(export \"main\""};
  failures += run_case("ret42", "int main(){return 42;}\n", basic, 4, 42);
  const char *arith[] = {"i32.const 29", "(return"};
  failures += run_case("arith", "int main(){return (3+4)*5-6;}\n", arith, 2, 29);
  const char *local[] = {"__stack_pointer", "i32.store", "i32.load"};
  failures += run_case("local", "int main(){int x; x=7; return x+1;}\n", local, 3, 8);
  const char *call[] = {"(func $add (param $p0 i32) (param $p1 i32) (result i32)", "(call $add"};
  failures += run_case("call", "int add(int a,int b){return a+b;} int main(){return add(3,4);}\n", call, 2, 7);
  const char *branch[] = {"(local $pc i32)", "(loop $dispatch", "(br $dispatch)"};
  failures += run_case("branch", "int main(){if(1)return 1; return 0;}\n", branch, 3, 1);
  const char *loop[] = {"(local $pc i32)", "(loop $dispatch", "i32.lt_s"};
  failures += run_case("loop", "int main(){int i; i=0; while(i<3){i=i+1;} return i;}\n", loop, 3, 3);
  failures += run_fail_case("fp", "int main(){return 1.5;}\n", "E4008");
  if (failures) return 1;
  printf("wasm32 backend tests passed\n");
  return 0;
}
