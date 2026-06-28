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
  const char *call[] = {"(func $add (param $p0 i64) (param $p1 i64) (result i32)", "(call $add"};
  failures += run_case("call", "int add(int a,int b){return a+b;} int main(){return add(3,4);}\n", call, 2, 7);
  const char *i64_call[] = {"(func $inc (param $p0 i64) (result i64)", "i64.load", "(call $inc"};
  failures += run_case("i64_call", "long inc(long x){return x+1;} int main(){return inc(41L);}\n",
                       i64_call, 3, 42);
  const char *branch[] = {"(local $pc i32)", "(loop $dispatch", "(br $dispatch)"};
  failures += run_case("branch", "int main(){if(1)return 1; return 0;}\n", branch, 3, 1);
  const char *loop[] = {"(local $pc i32)", "(loop $dispatch", "i32.lt_s"};
  failures += run_case("loop", "int main(){int i; i=0; while(i<3){i=i+1;} return i;}\n", loop, 3, 3);
  const char *for_loop[] = {"(local $pc i32)", "(loop $dispatch", "i32.lt_s"};
  failures += run_case("for_loop",
                       "int main(){int s; s=0; for(int i=0;i<5;i=i+1){s=s+i;} return s;}\n",
                       for_loop, 3, 10);
  const char *break_continue[] = {"(local $pc i32)", "(loop $dispatch", "(br $dispatch)"};
  failures += run_case("break_continue",
                       "int main(){int s; s=0; int i; i=0; while(i<6){i=i+1; "
                       "if(i==2)continue; if(i==5)break; s=s+i;} return s;}\n",
                       break_continue, 3, 8);
  const char *switch_case[] = {"(local $pc i32)", "(loop $dispatch", "i32.eq"};
  failures += run_case("switch_case",
                       "int main(){int x; x=3; switch(x){case 1:return 10; "
                       "case 3:return 30; default:return 99;} return 0;}\n",
                       switch_case, 3, 30);
  const char *ternary[] = {"(local $pc i32)", "(loop $dispatch", "i32.const 20"};
  failures += run_case("ternary", "int main(){int x; x=0; return x?10:20;}\n", ternary, 3, 20);
  const char *global_read[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_read", "int g=5; int main(){return g+2;}\n", global_read, 2, 7);
  const char *global_write[] = {"(data (i32.const", "i32.store"};
  failures += run_case("global_write", "int g; int main(){g=9; return g;}\n", global_write, 2, 9);
  const char *global_array[] = {"(data (i32.const", "\\0a\\00\\00\\00\\14\\00\\00\\00\\1e\\00\\00\\00"};
  failures += run_case("global_array", "int a[3]={10,20,30}; int main(){return a[1]+a[2];}\n",
                       global_array, 2, 50);
  const char *global_char_array[] = {"(data (i32.const", "\"abc\\00\""};
  failures += run_case("global_char_array", "char g[]=\"abc\"; int main(){return g[1];}\n",
                       global_char_array, 2, 98);
  const char *global_str_ptr[] = {"(data (i32.const", "\"abc\\00\""};
  failures += run_case("global_str_ptr", "char *p=\"abc\"; int main(){return p[1];}\n",
                       global_str_ptr, 2, 98);
  const char *global_str_ptr_offset[] = {"(data (i32.const", "\"abc\\00\""};
  failures += run_case("global_str_ptr_offset", "char *p=\"abc\"+1; int main(){return p[0];}\n",
                       global_str_ptr_offset, 2, 98);
  const char *global_str_ptr_array[] = {"(data (i32.const", "\"abc\\00\"", "\"de\\00\"", "i32.load"};
  failures += run_case("global_str_ptr_array",
                       "char *names[3]={\"abc\",\"de\",\"f\"}; int main(){return names[1][1]+names[2][0];}\n",
                       global_str_ptr_array, 4, 203);
  const char *global_str_ptr_array_offset[] = {"(data (i32.const", "\"abc\\00\"", "\"xyz\\00\""};
  failures += run_case("global_str_ptr_array_offset",
                       "char *p[2]={\"abc\"+2,\"xyz\"+1}; int main(){return p[0][0]+p[1][0];}\n",
                       global_str_ptr_array_offset, 3, 220);
  const char *global_ptr_array_addr[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_ptr_array_addr",
                       "int data[3]={4,5,6}; int *p[2]={&data[0],data+2}; int main(){return *p[0]+*p[1];}\n",
                       global_ptr_array_addr, 2, 10);
  const char *global_struct[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_struct",
                       "struct P{int a; int b;}; struct P g={3,4}; int main(){return g.a+g.b;}\n",
                       global_struct, 2, 7);
  const char *global_struct_partial[] = {"(data (i32.const", "\\07\\00\\00\\00"};
  failures += run_case("global_struct_partial",
                       "struct P{int a; int b; int c;}; struct P g={.b=7}; int main(){return g.a+g.b+g.c;}\n",
                       global_struct_partial, 2, 7);
  const char *global_nested_struct[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_nested_struct",
                       "struct I{int a; int b;}; struct O{struct I i; int c;}; "
                       "struct O g={{1,2},3}; int main(){return g.i.a+g.i.b+g.c;}\n",
                       global_nested_struct, 2, 6);
  const char *global_struct_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_struct_array",
                       "struct P{int a; int b;}; struct P g[2]={{1,2},{3,4}}; int main(){return g[1].a+g[1].b;}\n",
                       global_struct_array, 2, 7);
  const char *global_struct_char_array[] = {"(data (i32.const", "\"abc\\00\""};
  failures += run_case("global_struct_char_array",
                       "struct S{char name[4]; int id;}; struct S g={\"abc\",5}; int main(){return g.name[1]+g.id;}\n",
                       global_struct_char_array, 2, 103);
  const char *global_struct_str_ptr[] = {"(data (i32.const", "\"hi\\00\"", "i32.load"};
  failures += run_case("global_struct_str_ptr",
                       "struct S{char *name; int id;}; struct S g={\"hi\",5}; int main(){return g.name[0]+g.id;}\n",
                       global_struct_str_ptr, 3, 109);
  const char *global_struct_str_ptr_offset[] = {"(data (i32.const", "\"hello\\00\"", "i32.load"};
  failures += run_case("global_struct_str_ptr_offset",
                       "struct S{char *p;}; struct S g={\"hello\"+1}; int main(){return g.p[0];}\n",
                       global_struct_str_ptr_offset, 3, 101);
  const char *global_struct_bool[] = {"(data (i32.const", "\\01"};
  failures += run_case("global_struct_bool",
                       "struct S{_Bool b; int x;}; struct S g={100,4}; int main(){return g.b+g.x;}\n",
                       global_struct_bool, 2, 5);
  const char *global_struct_bitfield[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_struct_bitfield",
                       "struct S{unsigned int a:3; unsigned int b:5;}; struct S g={3,5}; "
                       "int main(){return g.a*10+g.b;}\n",
                       global_struct_bitfield, 2, 35);
  const char *string_lit[] = {"(data (i32.const", "\"abc\\00\"", "i32.load8_s"};
  failures += run_case("string_lit", "int main(){char *p=\"abc\"; return p[1];}\n", string_lit, 3, 98);
  const char *struct_copy[] = {"i64.store", "i64.load", "i32.store", "i32.load"};
  failures += run_case("struct_copy",
                       "struct P{int a; int b; int c;}; int main(){struct P x; struct P y; "
                       "x.a=3; x.b=4; x.c=5; y=x; return y.a+y.b+y.c;}\n",
                       struct_copy, 4, 12);
  const char *alignas32[] = {"i32.and", "i32.const -32"};
  failures += run_case("alignas32",
                       "int main(){_Alignas(32) int x; x=7; return x + (((long)&x) & 31);}\n",
                       alignas32, 2, 7);
  const char *fp_return_to_int[] = {"f64.const", "i32.trunc_f64_s"};
  failures += run_case("fp_return_to_int", "int main(){return 1.5;}\n",
                       fp_return_to_int, 2, 1);
  failures += run_fail_case("external_call", "int main(){return puts(\"x\");}\n", "E4008");
  failures += run_fail_case("funcptr_init",
                            "int f(){return 1;} int (*fp[1])()={f}; int main(){return 0;}\n",
                            "E4008");
  const char *global_fp_scalar[] = {"(data (i32.const", "f64.load"};
  failures += run_case("global_fp_scalar", "double g=2.5; int main(){return (int)(g+1.5);}\n",
                       global_fp_scalar, 2, 4);
  const char *global_fp_array[] = {"(data (i32.const", "f32.load"};
  failures += run_case("global_fp_array", "float g[2]={1.25f,2.75f}; int main(){return (int)(g[0]+g[1]);}\n",
                       global_fp_array, 2, 4);
  const char *global_struct_fp[] = {"(data (i32.const", "f64.load", "i32.load"};
  failures += run_case("global_struct_fp",
                       "struct S{double d; int x;}; struct S g={1.5,2}; int main(){return (int)g.d+g.x;}\n",
                       global_struct_fp, 3, 3);
  const char *global_union_int[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_int",
                       "union U{int i; char c;}; union U g={.i=77}; int main(){return g.i;}\n",
                       global_union_int, 2, 77);
  const char *global_union_fp[] = {"(data (i32.const", "f64.load"};
  failures += run_case("global_union_fp",
                       "union U{int i; double d;}; union U g={.d=2.5}; int main(){return (int)(g.d+1.5);}\n",
                       global_union_fp, 2, 4);
  const char *global_union_struct[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_struct",
                       "struct S{int a; int b;}; union U{struct S s; int i;}; "
                       "union U g={.s={10,32}}; int main(){return g.s.a+g.s.b;}\n",
                       global_union_struct, 2, 42);
  const char *global_union_struct_fp[] = {"(data (i32.const", "f64.load"};
  failures += run_case("global_union_struct_fp",
                       "struct S{double d; int x;}; union U{struct S s; long raw;}; "
                       "union U g={.s={2.5,4}}; int main(){return (int)g.s.d+g.s.x;}\n",
                       global_union_struct_fp, 2, 6);
  const char *global_struct_union[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_struct_union",
                       "union U{int i; char c;}; struct S{int tag; union U u;}; "
                       "struct S g={7,{.i=35}}; int main(){return g.tag+g.u.i;}\n",
                       global_struct_union, 2, 42);
  const char *global_struct_union_fp[] = {"(data (i32.const", "f64.load"};
  failures += run_case("global_struct_union_fp",
                       "union U{int i; double d;}; struct S{union U u; int x;}; "
                       "struct S g={{.d=2.5},4}; int main(){return (int)g.u.d+g.x;}\n",
                       global_struct_union_fp, 2, 6);
  const char *global_struct_union_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_struct_union_array",
                       "union U{int i; char c;}; struct S{union U u[2];}; "
                       "struct S g={{{.i=10},{.i=32}}}; int main(){return g.u[0].i+g.u[1].i;}\n",
                       global_struct_union_array, 2, 42);
  const char *ptr_i64_mix[] = {"i64.extend_i32_u", "i64.add", "i64.eq"};
  failures += run_case("ptr_i64_mix",
                       "int main(){unsigned int x; x=4294967295U; unsigned long y; "
                       "y=x+1UL; return y==4294967296UL;}\n",
                       ptr_i64_mix, 3, 1);
  const char *double_arith[] = {"f64.const", "f64.add", "f64.mul", "i32.trunc_f64_s"};
  failures += run_case("double_arith",
                       "int main(){double x; x=1.5; double y; y=2.5; return (int)((x+y)*2.0);}\n",
                       double_arith, 4, 8);
  const char *float_arith[] = {"f32.const", "f32.add", "i32.trunc_f32_s"};
  failures += run_case("float_arith",
                       "int main(){float x; x=3.5f; return (int)(x+1.5f);}\n",
                       float_arith, 3, 5);
  const char *double_cmp[] = {"f64.lt"};
  failures += run_case("double_cmp", "int main(){double x; x=2.0; return x<3.0;}\n",
                       double_cmp, 1, 1);
  const char *double_call[] = {"(func $addd (param $p0 f64) (param $p1 f64) (result f64)",
                               "(call $addd", "i32.trunc_f64_s"};
  failures += run_case("double_call",
                       "double addd(double a,double b){return a+b;} int main(){return (int)addd(1.25,2.75);}\n",
                       double_call, 3, 4);
  const char *double_neg[] = {"f64.neg"};
  failures += run_case("double_neg", "int main(){double x; x=-2.0; return (int)(-x);}\n",
                       double_neg, 1, 2);
  if (failures) return 1;
  printf("wasm32 backend tests passed\n");
  return 0;
}
