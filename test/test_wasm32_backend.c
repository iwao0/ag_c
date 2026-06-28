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

static int run_case(const char *name, const char *src, const char **needles, int nneedles) {
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
  return 0;
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
  failures += run_case("ret42", "int main(){return 42;}\n", basic, 4);
  const char *arith[] = {"i32.const 29", "(return"};
  failures += run_case("arith", "int main(){return (3+4)*5-6;}\n", arith, 2);
  const char *local[] = {"__stack_pointer", "i32.store", "i32.load"};
  failures += run_case("local", "int main(){int x; x=7; return x+1;}\n", local, 3);
  const char *call[] = {"(func $add (param $p0 i32) (param $p1 i32) (result i32)", "(call $add"};
  failures += run_case("call", "int add(int a,int b){return a+b;} int main(){return add(3,4);}\n", call, 2);
  failures += run_fail_case("branch", "int main(){if(1)return 1; return 0;}\n", "E4008");
  if (failures) return 1;
  printf("wasm32 backend tests passed\n");
  return 0;
}
