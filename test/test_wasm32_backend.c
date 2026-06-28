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
  const char *void_call[] = {"(func $set (param $p0 i64)", "(call $set", "i32.store"};
  failures += run_case("void_call",
                       "void set(int *p){*p=7;} int main(){int x; x=0; set(&x); return x;}\n",
                       void_call, 3, 7);
  const char *void_return[] = {"(func $noop\n", "(call $noop)", "return"};
  failures += run_case("void_return", "void noop(){return;} int main(){noop(); return 9;}\n",
                       void_return, 3, 9);
  const char *i64_call[] = {"(func $inc (param $p0 i64) (result i64)", "i64.load", "(call $inc"};
  failures += run_case("i64_call", "long inc(long x){return x+1;} int main(){return inc(41L);}\n",
                       i64_call, 3, 42);
  const char *i64_local_store[] = {"i64.store", "i64.load", "i64.shr_s"};
  failures += run_case("i64_local_store",
                       "int main(){long x; x=1L<<40; return (x>>40)==1;}\n",
                       i64_local_store, 3, 1);
  const char *i64_global_store[] = {"i64.store", "i64.load", "i64.shr_s"};
  failures += run_case("i64_global_store",
                       "long g; int main(){g=1L<<40; return (g>>40)==1;}\n",
                       i64_global_store, 3, 1);
  const char *i64_deref_store[] = {"i64.store", "i64.load", "i64.shr_s"};
  failures += run_case("i64_deref_store",
                       "int main(){long x; long *p; p=&x; *p=1L<<40; return ((*p)>>40)==1;}\n",
                       i64_deref_store, 3, 1);
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
  const char *global_large_zero_array[] = {"i32.store", "i32.load"};
  failures += run_case("global_large_zero_array",
                       "int a[2][3]; int main(){a[1][2]=77; return a[0][0]+a[1][2];}\n",
                       global_large_zero_array, 2, 77);
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
  const char *struct4_return_member[] = {"(func $make (result i32)", "(call $make", "i32.store"};
  failures += run_case("struct4_return_member",
                       "struct S{int x;}; struct S make(){struct S s; s.x=7; return s;} "
                       "int main(){return make().x;}\n",
                       struct4_return_member, 3, 7);
  const char *struct4_return_arg[] = {"(func $take (param $p0 i32) (result i32)", "(call $make", "(call $take"};
  failures += run_case("struct4_return_arg",
                       "struct S{int x;}; int take(struct S s){return s.x+1;} "
                       "struct S make(){struct S s; s.x=7; return s;} int main(){return take(make());}\n",
                       struct4_return_arg, 3, 8);
  const char *struct1_return_arg[] = {"(func $take (param $p0 i32) (result i32)", "(call $make", "(call $take"};
  failures += run_case("struct1_return_arg",
                       "struct S{unsigned char x;}; int take(struct S s){return s.x+1;} "
                       "struct S make(){struct S s; s.x=255; return s;} int main(){return take(make());}\n",
                       struct1_return_arg, 3, 256);
  const char *struct2_return_member[] = {"(func $make (result i32)", "(call $make", "i32.store16"};
  failures += run_case("struct2_return_member",
                       "struct S{short x;}; struct S make(){struct S s; s.x=300; return s;} "
                       "int main(){return make().x;}\n",
                       struct2_return_member, 3, 300);
  const char *struct8_return[] = {"(func $make (result i64)", "i64.load", "i64.store"};
  failures += run_case("struct8_return",
                       "struct P{int a; int b;}; struct P make(){struct P p; p.a=3; p.b=4; "
                       "return p;} int main(){struct P q; q=make(); return q.a+q.b;}\n",
                       struct8_return, 3, 7);
  const char *struct8_return_deref[] = {"(func $make (param $p0 i64) (result i64)", "i64.load", "i64.store"};
  failures += run_case("struct8_return_deref",
                       "struct P{int a; int b;}; struct P make(struct P *p){return *p;} "
                       "int main(){struct P q; q.a=8; q.b=9; struct P r; r=make(&q); return r.a+r.b;}\n",
                       struct8_return_deref, 3, 17);
  const char *struct8_return_ternary[] = {"(func $pick (param $p0 i64) (result i64)", "if", "i64.load"};
  failures += run_case("struct8_return_ternary",
                       "struct P{int a; int b;}; struct P pick(int c){struct P a; struct P b; "
                       "a.a=1; a.b=2; b.a=3; b.b=4; return c?b:a;} "
                       "int main(){struct P q; q=pick(1); return q.a+q.b;}\n",
                       struct8_return_ternary, 3, 7);
  const char *struct8_arg[] = {"(func $id (param $p0 i64) (result i64)", "(call $id", "i64.load"};
  failures += run_case("struct8_arg",
                       "struct P{int a; int b;}; struct P id(struct P p){return p;} "
                       "int main(){struct P q; q.a=2; q.b=5; struct P r; r=id(q); return r.a+r.b;}\n",
                       struct8_arg, 3, 7);
  const char *struct8_arg_compound[] = {"(func $sum (param $p0 i64) (result i32)", "(call $sum", "i64.load"};
  failures += run_case("struct8_arg_compound",
                       "struct P{int a; int b;}; int sum(struct P p){return p.a+p.b;} "
                       "int main(){return sum((struct P){6,7});}\n",
                       struct8_arg_compound, 3, 13);
  const char *struct8_arg_ternary[] = {"(func $sum (param $p0 i64) (result i32)", "if", "i64.load"};
  failures += run_case("struct8_arg_ternary",
                       "struct P{int a; int b;}; int sum(struct P p){return p.a+p.b;} "
                       "int main(){struct P a; struct P b; a.a=1; a.b=2; b.a=3; b.b=4; "
                       "return sum(1?b:a);}\n",
                       struct8_arg_ternary, 3, 7);
  const char *struct_arg_ternary[] = {"(func $sum (param $p0 i32) (result i32)", "i64.store", "(call $sum"};
  failures += run_case("struct_arg_ternary",
                       "struct P{int a; int b; int c;}; int sum(struct P p){return p.a+p.b+p.c;} "
                       "int main(){struct P a; struct P b; a.a=1; a.b=2; a.c=3; "
                       "b.a=4; b.b=5; b.c=6; return sum(1?b:a);}\n",
                       struct_arg_ternary, 3, 15);
  const char *struct8_assign_ternary[] = {"i64.store", "if", "i64.load"};
  failures += run_case("struct8_assign_ternary",
                       "struct P{int a; int b;}; int main(){struct P a; struct P b; struct P q; "
                       "a.a=1; a.b=2; b.a=3; b.b=4; q=1?b:a; return q.a+q.b;}\n",
                       struct8_assign_ternary, 3, 7);
  const char *struct_return_ternary[] = {"(func $pick (param $p0 i32)", "(call $pick", "i64.store"};
  failures += run_case("struct_return_ternary",
                       "struct P{int a; int b; int c;}; struct P pick(int c){struct P a; struct P b; "
                       "a.a=1; a.b=2; a.c=3; b.a=4; b.b=5; b.c=6; return c?b:a;} "
                       "int main(){struct P q; q=pick(1); return q.a+q.b+q.c;}\n",
                       struct_return_ternary, 3, 15);
  const char *struct_return_member[] = {"(func $make (param $p0 i32)", "(call $make", "i32.load"};
  failures += run_case("struct_return_member",
                       "struct P{int a; int b; int c;}; struct P make(){struct P p; "
                       "p.a=3; p.b=4; p.c=5; return p;} int main(){return make().b;}\n",
                       struct_return_member, 3, 4);
  const char *struct_return_arg[] = {"(func $sum (param $p0 i32) (result i32)", "(call $make", "(call $sum"};
  failures += run_case("struct_return_arg",
                       "struct P{int a; int b; int c;}; struct P make(){struct P p; "
                       "p.a=3; p.b=4; p.c=5; return p;} int sum(struct P p){return p.a+p.b+p.c;} "
                       "int main(){return sum(make());}\n",
                       struct_return_arg, 3, 12);
  const char *struct_return_ternary_arg[] = {"(func $sum (param $p0 i32) (result i32)", "if", "(call $sum"};
  failures += run_case("struct_return_ternary_arg",
                       "struct P{int a; int b; int c;}; struct P make(int x){struct P p; "
                       "p.a=x; p.b=x+1; p.c=x+2; return p;} int sum(struct P p){return p.a+p.b+p.c;} "
                       "int main(){return sum(1?make(4):make(1));}\n",
                       struct_return_ternary_arg, 3, 15);
  const char *struct_assign_ternary[] = {"i64.store", "if", "i32.store"};
  failures += run_case("struct_assign_ternary",
                       "struct P{int a; int b; int c;}; int main(){struct P a; struct P b; struct P q; "
                       "a.a=1; a.b=2; a.c=3; b.a=4; b.b=5; b.c=6; q=1?b:a; return q.a+q.b+q.c;}\n",
                       struct_assign_ternary, 3, 15);
  const char *alignas32[] = {"i32.and", "i32.const -32"};
  failures += run_case("alignas32",
                       "int main(){_Alignas(32) int x; x=7; return x + (((long)&x) & 31);}\n",
                       alignas32, 2, 7);
  const char *fp_return_to_int[] = {"f64.const", "i32.trunc_f64_s"};
  failures += run_case("fp_return_to_int", "int main(){return 1.5;}\n",
                       fp_return_to_int, 2, 1);
  failures += run_fail_case("external_call", "int main(){return puts(\"x\");}\n", "E4008");
  const char *funcptr_init[] = {"(data (i32.const", "(table 1 funcref)", "(elem (i32.const 0) $f"};
  failures += run_case("funcptr_init",
                       "int f(){return 1;} int (*fp[1])()={f}; int main(){return fp[0]();}\n",
                       funcptr_init, 3, 1);
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
  const char *global_union_bitfield[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_bitfield",
                       "union U{unsigned int a:3; unsigned int b:5;}; union U g={.b=17}; int main(){return g.b;}\n",
                       global_union_bitfield, 2, 17);
  const char *global_union_signed_bitfield[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_signed_bitfield",
                       "union U{int a:3; unsigned int b:5;}; union U g={.a=-1}; int main(){return g.a+1;}\n",
                       global_union_signed_bitfield, 2, 0);
  const char *global_union_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_array",
                       "union U{int i; char c;}; union U g[2]={{.i=10},{.i=32}}; "
                       "int main(){return g[0].i+g[1].i;}\n",
                       global_union_array, 2, 42);
  const char *global_union_fp_array[] = {"(data (i32.const", "f64.load"};
  failures += run_case("global_union_fp_array",
                       "union U{int i; double d;}; union U g[2]={{.d=1.25},{.d=2.75}}; "
                       "int main(){return (int)(g[0].d+g[1].d);}\n",
                       global_union_fp_array, 2, 4);
  const char *global_union_mixed_fp_array[] = {"(data (i32.const", "f64.load", "i32.load"};
  failures += run_case("global_union_mixed_fp_array",
                       "union U{int i; double d;}; union U g[2]={{.i=10},{.d=2.5}}; "
                       "int main(){return g[0].i+(int)g[1].d;}\n",
                       global_union_mixed_fp_array, 3, 12);
  const char *global_union_mixed_fp_array_rev[] = {"(data (i32.const", "f64.load", "i32.load"};
  failures += run_case("global_union_mixed_fp_array_rev",
                       "union U{double d; int i;}; union U g[2]={{.d=2.5},{.i=10}}; "
                       "int main(){return (int)g[0].d+g[1].i;}\n",
                       global_union_mixed_fp_array_rev, 3, 12);
  const char *global_union_struct_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_struct_array",
                       "struct S{int a; int b;}; union U{struct S s; int i;}; "
                       "union U g[2]={{.s={10,11}},{.s={20,1}}}; int main(){return g[0].s.a+g[1].s.a+g[1].s.b;}\n",
                       global_union_struct_array, 2, 31);
  const char *global_union_mixed_struct_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_mixed_struct_array",
                       "struct P{int a; int b;}; union U{struct P p; int i;}; "
                       "union U g[2]={{.i=10},{.p={20,1}}}; int main(){return g[0].i+g[1].p.a+g[1].p.b;}\n",
                       global_union_mixed_struct_array, 2, 31);
  const char *global_union_mixed_struct_array_rev[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_mixed_struct_array_rev",
                       "struct P{int a; int b;}; union U{struct P p; int i;}; "
                       "union U g[2]={{.p={20,1}},{.i=10}}; int main(){return g[0].p.a+g[0].p.b+g[1].i;}\n",
                       global_union_mixed_struct_array_rev, 2, 31);
  const char *global_union_bitfield_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_union_bitfield_array",
                       "union U{unsigned int a:3; unsigned int b:5;}; union U g[2]={{.b=17},{.b=25}}; "
                       "int main(){return g[0].b+g[1].b;}\n",
                       global_union_bitfield_array, 2, 42);
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
  const char *global_struct_union_struct[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_struct_union_struct",
                       "struct P{int a; int b;}; union U{struct P p; int i;}; struct S{int tag; union U u;}; "
                       "struct S g={7,{.p={10,25}}}; int main(){return g.tag+g.u.p.a+g.u.p.b;}\n",
                       global_struct_union_struct, 2, 42);
  const char *global_struct_union_bitfield[] = {"(data (i32.const", "i32.load"};
  failures += run_case("global_struct_union_bitfield",
                       "union U{unsigned int a:3; unsigned int b:5;}; struct S{int tag; union U u;}; "
                       "struct S g={25,{.b=17}}; int main(){return g.tag+g.u.b;}\n",
                       global_struct_union_bitfield, 2, 42);
  const char *static_struct_mixed_union[] = {"(data (i32.const", "i32.load"};
  failures += run_case("static_struct_mixed_union",
                       "struct P{int a; int b;}; union U{struct P p; int i;}; struct W{union U a; union U b;}; "
                       "int main(){static struct W w={{.i=10},{.p={20,1}}}; return w.a.i+w.b.p.a+w.b.p.b;}\n",
                       static_struct_mixed_union, 2, 31);
  const char *static_union_mixed_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("static_union_mixed_array",
                       "struct P{int a; int b;}; union U{struct P p; int i;}; "
                       "int main(){static union U g[2]={{.i=10},{.p={20,1}}}; return g[0].i+g[1].p.a+g[1].p.b;}\n",
                       static_union_mixed_array, 2, 31);
  const char *static_struct_array_mixed_union[] = {"(data (i32.const", "i32.load"};
  failures += run_case("static_struct_array_mixed_union",
                       "struct P{int a; int b;}; union U{struct P p; int i;}; struct S{union U u; int tail;}; "
                       "int main(){static struct S a[2]={{{.i=10},1},{{.p={20,2}},3}}; "
                       "return a[0].u.i+a[0].tail+a[1].u.p.a+a[1].u.p.b+a[1].tail;}\n",
                       static_struct_array_mixed_union, 2, 36);
  const char *static_struct_array_persistent[] = {"(data (i32.const", "i32.store", "i32.load"};
  failures += run_case("static_struct_array_persistent",
                       "struct P{int a;}; int f(){static struct P a[1]={{1}}; "
                       "a[0].a=a[0].a+1; return a[0].a;} int main(){return f()*10+f();}\n",
                       static_struct_array_persistent, 3, 23);
  const char *static_struct_incomplete_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("static_struct_incomplete_array",
                       "struct P{int a; int b;}; int main(){static struct P a[]={{1,2},{3,4}}; "
                       "return a[1].a+a[1].b;}\n",
                       static_struct_incomplete_array, 2, 7);
  const char *static_union_incomplete_array[] = {"(data (i32.const", "i32.load"};
  failures += run_case("static_union_incomplete_array",
                       "struct P{int a; int b;}; union U{struct P p; int i;}; "
                       "int main(){static union U u[]={{.i=10},{.p={20,1}}}; return u[0].i+u[1].p.a+u[1].p.b;}\n",
                       static_union_incomplete_array, 2, 31);
  const char *static_char_string_array[] = {"(data (i32.const", "\"abc\\00\"", "i32.load8_s"};
  failures += run_case("static_char_string_array",
                       "int main(){static char s[]=\"abc\"; return s[1];}\n",
                       static_char_string_array, 3, 98);
  const char *static_char_string_persistent[] = {"(data (i32.const", "\"az\\00\"", "i32.store8"};
  failures += run_case("static_char_string_persistent",
                       "int f(){static char s[]=\"az\"; s[0]=s[0]+1; return s[0]+s[1];} "
                       "int main(){return f()*1000+f();}\n",
                       static_char_string_persistent, 3, 220221);
  const char *static_paren_char_string_array[] = {"(data (i32.const", "\"abc\\00\"", "i32.load8_s"};
  failures += run_case("static_paren_char_string_array",
                       "int main(){static char (s[])=\"abc\"; return s[1];}\n",
                       static_paren_char_string_array, 3, 98);
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
  const char *funcptr_call[] = {"(table 1 funcref)", "(elem (i32.const 0) $add1", "call_indirect"};
  failures += run_case("funcptr_call",
                       "int add1(int x){return x+1;} int main(){int (*fp)(int); fp=add1; return fp(41);}\n",
                       funcptr_call, 3, 42);
  const char *funcptr_double_call[] = {"(table 1 funcref)", "call_indirect", "(param f64)", "(result f64)"};
  failures += run_case("funcptr_double_call",
                       "double addd(double x,double y){return x+y;} int main(){double (*fp)(double,double); "
                       "fp=addd; return (int)fp(1.25,2.75);}\n",
                       funcptr_double_call, 4, 4);
  const char *funcptr_int_to_double_arg[] = {"f64.convert_i32_s", "call_indirect", "(param f64)"};
  failures += run_case("funcptr_int_to_double_arg",
                       "double add(double x){return x+0.5;} int main(){double (*fp)(double); "
                       "fp=add; return (int)fp(3);}\n",
                       funcptr_int_to_double_arg, 3, 3);
  const char *funcptr_double_to_int_arg[] = {"i32.trunc_f64_s", "call_indirect", "(param i64)"};
  failures += run_case("funcptr_double_to_int_arg",
                       "int take(int x){return x;} int main(){int (*fp)(int); fp=take; return fp(7.9);}\n",
                       funcptr_double_to_int_arg, 3, 7);
  const char *funcptr_pointer_return[] = {"(call_indirect (result i32)", "(return (local.get"};
  failures += run_case("funcptr_pointer_return",
                       "int g; int *get(void){return &g;} int main(){int *(*fp)(void); fp=get; "
                       "*fp()=42; return g;}\n",
                       funcptr_pointer_return, 2, 42);
  const char *funcptr_pointer_to_array_return[] = {"(call_indirect (result i32)", "i32.store"};
  failures += run_case("funcptr_pointer_to_array_return",
                       "int a[2][3]; int (*get(void))[3]{return a;} "
                       "int main(){int (*(*fp)(void))[3]; fp=get; fp()[1][2]=77; return a[1][2];}\n",
                       funcptr_pointer_to_array_return, 2, 77);
  const char *funcptr_pointer_to_double_array_return[] = {"(call_indirect (result i32)", "f64.store"};
  failures += run_case("funcptr_pointer_to_double_array_return",
                       "double a[2][2]; double (*get(void))[2]{return a;} "
                       "int main(){double (*(*fp)(void))[2]; fp=get; fp()[1][1]=4.5; "
                       "return (int)a[1][1];}\n",
                       funcptr_pointer_to_double_array_return, 2, 4);
  const char *funcptr_void_call[] = {"call_indirect", "(table 1 funcref)"};
  failures += run_case("funcptr_void_call",
                       "void set(int *p){*p=9;} int main(){void (*fp)(int*); int x; x=0; "
                       "fp=set; fp(&x); return x;}\n",
                       funcptr_void_call, 2, 9);
  const char *funcptr_unused_result_call[] = {"drop", "call_indirect", "(result i32)"};
  failures += run_case("funcptr_unused_result_call",
                       "int set(int *p){*p=9; return 123;} int main(){int (*fp)(int*); int x; x=0; "
                       "fp=set; fp(&x); return x;}\n",
                       funcptr_unused_result_call, 3, 9);
  failures += run_fail_case("funcptr_external_ref",
                            "int ext(int); int main(){int (*fp)(int); fp=ext; return fp(1);}\n",
                            "E4008");
  const char *global_funcptr_call[] = {"(data (i32.const", "(table 1 funcref)", "call_indirect"};
  failures += run_case("global_funcptr_call",
                       "int add1(int x){return x+1;} int (*g)(int)=add1; int main(){return g(41);}\n",
                       global_funcptr_call, 3, 42);
  const char *global_funcptr_array_call[] = {"(data (i32.const", "(table 2 funcref)",
                                             "(elem (i32.const 0) $add1 $add2", "call_indirect"};
  failures += run_case("global_funcptr_array_call",
                       "int add1(int x){return x+1;} int add2(int x){return x+2;} "
                       "int (*ops[2])(int)={add1,add2}; int main(){return ops[1](40);}\n",
                       global_funcptr_array_call, 4, 42);
  const char *static_funcptr_call[] = {"(i32.const 0)", "(table 1 funcref)", "call_indirect"};
  failures += run_case("static_funcptr_call",
                       "int add1(int x){return x+1;} int main(){static int (*fp)(int)=add1; "
                       "return fp(41);}\n",
                       static_funcptr_call, 3, 42);
  const char *struct_funcptr_call[] = {"(data (i32.const", "(table 1 funcref)", "call_indirect"};
  failures += run_case("struct_funcptr_call",
                       "int add1(int x){return x+1;} struct Ops{int (*f)(int);}; "
                       "struct Ops ops={add1}; int main(){return ops.f(41);}\n",
                       struct_funcptr_call, 3, 42);
  const char *struct_funcptr_offset_call[] = {"(data (i32.const", "(table 1 funcref)", "call_indirect"};
  failures += run_case("struct_funcptr_offset_call",
                       "int add1(int x){return x+1;} struct Ops{int pad; int (*f)(int);}; "
                       "struct Ops ops={5,add1}; int main(){return ops.f(41);}\n",
                       struct_funcptr_offset_call, 3, 42);
  const char *struct_funcptr_array_member_call[] = {"(data (i32.const", "(table 2 funcref)",
                                                     "(elem (i32.const 0) $add1 $add2",
                                                     "call_indirect"};
  failures += run_case("struct_funcptr_array_member_call",
                       "int add1(int x){return x+1;} int add2(int x){return x+2;} "
                       "struct Ops{int (*f[2])(int);}; struct Ops ops={{add1,add2}}; "
                       "int main(){return ops.f[1](40);}\n",
                       struct_funcptr_array_member_call, 4, 42);
  const char *struct_funcptr_array_member_store_call[] = {"(table 2 funcref)", "$add1 $add2",
                                                           "call_indirect"};
  failures += run_case("struct_funcptr_array_member_store_call",
                       "int add1(int x){return x+1;} int add2(int x){return x+2;} "
                       "struct Ops{int (*f[2])(int);}; struct Ops ops={{add2,add2}}; "
                       "int main(){ops.f[1]=add1; return ops.f[1](41);}\n",
                       struct_funcptr_array_member_store_call, 3, 42);
  const char *struct_funcptr_void_call[] = {"call_indirect", "(table 1 funcref)"};
  failures += run_case("struct_funcptr_void_call",
                       "void set(int *p){*p=9;} struct Ops{void (*f)(int*);}; "
                       "struct Ops ops={set}; int main(){int x; x=0; ops.f(&x); return x;}\n",
                       struct_funcptr_void_call, 2, 9);
  const char *struct_funcptr_unused_result_call[] = {"drop", "call_indirect", "(result i32)"};
  failures += run_case("struct_funcptr_unused_result_call",
                       "int set(int *p){*p=9; return 123;} struct Ops{int (*f)(int*);}; "
                       "struct Ops ops={set}; int main(){int x; x=0; ops.f(&x); return x;}\n",
                       struct_funcptr_unused_result_call, 3, 9);
  const char *struct_funcptr_store_call[] = {"(table 2 funcref)", "$set7 $set9", "call_indirect"};
  failures += run_case("struct_funcptr_store_call",
                       "void set9(int *p){*p=9;} void set7(int *p){*p=7;} "
                       "struct Ops{void (*f)(int*);}; struct Ops ops={set9}; "
                       "int main(){int x; x=0; ops.f=set7; ops.f(&x); return x;}\n",
                       struct_funcptr_store_call, 3, 7);
  failures += run_fail_case("struct_funcptr_control_flow_store",
                            "void set9(int *p){*p=9;} void set7(int *p){*p=7;} "
                            "struct Ops{void (*f)(int*);}; struct Ops ops={set9}; "
                            "int main(){int x; x=0; if(1) ops.f=set7; ops.f(&x); return x;}\n",
                            "E4008");
  failures += run_fail_case("global_funcptr_external_ref",
                            "int ext(int); int (*g)(int)=ext; int main(){return 0;}\n",
                            "E4008");
  const char *global_funcptr_void_call[] = {"call_indirect", "(table 1 funcref)"};
  failures += run_case("global_funcptr_void_call",
                       "void set(int *p){*p=9;} void (*g)(int*)=set; "
                       "int main(){int x; x=0; g(&x); return x;}\n",
                       global_funcptr_void_call, 2, 9);
  const char *global_funcptr_void_store_call[] = {"(table 2 funcref)", "$set7 $set9", "call_indirect"};
  failures += run_case("global_funcptr_void_store_call",
                       "void set9(int *p){*p=9;} void set7(int *p){*p=7;} "
                       "void (*g)(int*)=set9; int main(){int x; x=0; g=set7; g(&x); return x;}\n",
                       global_funcptr_void_store_call, 3, 7);
  const char *global_funcptr_unused_result_call[] = {"drop", "call_indirect", "(result i32)"};
  failures += run_case("global_funcptr_unused_result_call",
                       "int set(int *p){*p=9; return 123;} int (*g)(int*)=set; "
                       "int main(){int x; x=0; g(&x); return x;}\n",
                       global_funcptr_unused_result_call, 3, 9);
  failures += run_fail_case("global_funcptr_control_flow_store",
                            "void set9(int *p){*p=9;} void set7(int *p){*p=7;} "
                            "void (*g)(int*)=set9; int main(){int x; x=0; if(1) g=set7; "
                            "g(&x); return x;}\n",
                            "E4008");
  const char *unsigned_int_to_double[] = {"f64.convert_i32_u", "f64.lt"};
  failures += run_case("unsigned_int_to_double",
                       "int main(){unsigned int x; x=4294967295U; double d; d=x; "
                       "return d>4294967294.0;}\n",
                       unsigned_int_to_double, 2, 1);
  const char *double_to_unsigned_int[] = {"i32.trunc_f64_u", "i32.eq"};
  failures += run_case("double_to_unsigned_int",
                       "int main(){double d; d=4294967295.0; unsigned int x; x=d; "
                       "return x==4294967295U;}\n",
                       double_to_unsigned_int, 2, 1);
  const char *unsigned_int_to_double_call[] = {"f64.convert_i32_u", "(call $id"};
  failures += run_case("unsigned_int_to_double_call",
                       "double id(double x){return x;} int main(){unsigned int x; "
                       "x=4294967295U; return id(x)>4294967294.0;}\n",
                       unsigned_int_to_double_call, 2, 1);
  const char *double_to_unsigned_int_call[] = {"i64.trunc_f64_u", "(call $take"};
  failures += run_case("double_to_unsigned_int_call",
                       "int take(unsigned int x){return (int)(x>>31);} "
                       "int main(){return take(4294967295.0);}\n",
                       double_to_unsigned_int_call, 2, 1);
  const char *unsigned_int_to_double_return[] = {"f64.convert_i32_u", "(call $f"};
  failures += run_case("unsigned_int_to_double_return",
                       "double f(){unsigned int x; x=4294967295U; return x;} "
                       "int main(){return f()>4294967294.0;}\n",
                       unsigned_int_to_double_return, 2, 1);
  const char *double_to_unsigned_int_return[] = {"i32.trunc_f64_u", "(call $f"};
  failures += run_case("double_to_unsigned_int_return",
                       "unsigned int f(){return 4294967295.0;} int main(){return (int)(f()>>31);}\n",
                       double_to_unsigned_int_return, 2, 1);
  const char *double_neg[] = {"f64.neg"};
  failures += run_case("double_neg", "int main(){double x; x=-2.0; return (int)(-x);}\n",
                       double_neg, 1, 2);
  if (failures) return 1;
  printf("wasm32 backend tests passed\n");
  return 0;
}
