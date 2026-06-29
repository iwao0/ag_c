#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static int run_cmd(const char *cmd, const char *name) {
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "FAIL: %s failed (rc=%d): %s\n", name, rc, cmd);
    return 1;
  }
  return 0;
}

static int run_objdump_check(const char *name, const char *src,
                             const char **needles, int nneedles) {
  char c_path[256];
  char o_path[256];
  char dump_path[256];
  snprintf(c_path, sizeof(c_path), "build/wasm32_obj/%s.c", name);
  snprintf(o_path, sizeof(o_path), "build/wasm32_obj/%s.o", name);
  snprintf(dump_path, sizeof(dump_path), "build/wasm32_obj/%s.objdump", name);
  if (write_file(c_path, src) != 0) {
    fprintf(stderr, "FAIL: write %s\n", c_path);
    return 1;
  }
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "./build/ag_c_wasm -c -o %s %s", o_path, c_path);
  if (run_cmd(cmd, name) != 0) return 1;

  if (!command_available("wasm-objdump")) return 0;
  snprintf(cmd, sizeof(cmd), "wasm-objdump -x -d %s > %s", o_path, dump_path);
  if (run_cmd(cmd, "wasm-objdump") != 0) return 1;
  char buf[65536];
  if (slurp(dump_path, buf, sizeof(buf)) != 0) return 1;
  for (int i = 0; i < nneedles; i++) {
    if (!strstr(buf, needles[i])) {
      fprintf(stderr, "FAIL: %s objdump missing '%s'\n", name, needles[i]);
      return 1;
    }
  }
  return 0;
}

static int run_objdump_check_absent(const char *name, const char *src,
                                    const char **needles, int nneedles,
                                    const char **rejects, int nrejects) {
  char c_path[256];
  char o_path[256];
  char dump_path[256];
  snprintf(c_path, sizeof(c_path), "build/wasm32_obj/%s.c", name);
  snprintf(o_path, sizeof(o_path), "build/wasm32_obj/%s.o", name);
  snprintf(dump_path, sizeof(dump_path), "build/wasm32_obj/%s.objdump", name);
  if (write_file(c_path, src) != 0) {
    fprintf(stderr, "FAIL: write %s\n", c_path);
    return 1;
  }
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "./build/ag_c_wasm -c -o %s %s", o_path, c_path);
  if (run_cmd(cmd, name) != 0) return 1;

  if (!command_available("wasm-objdump")) return 0;
  snprintf(cmd, sizeof(cmd), "wasm-objdump -x -d %s > %s", o_path, dump_path);
  if (run_cmd(cmd, "wasm-objdump") != 0) return 1;
  char buf[65536];
  if (slurp(dump_path, buf, sizeof(buf)) != 0) return 1;
  for (int i = 0; i < nneedles; i++) {
    if (!strstr(buf, needles[i])) {
      fprintf(stderr, "FAIL: %s objdump missing '%s'\n", name, needles[i]);
      return 1;
    }
  }
  for (int i = 0; i < nrejects; i++) {
    if (strstr(buf, rejects[i])) {
      fprintf(stderr, "FAIL: %s objdump unexpectedly contains '%s'\n", name, rejects[i]);
      return 1;
    }
  }
  return 0;
}

static int run_fail_case(const char *name, const char *cmd, const char *needle) {
  char log_path[256];
  snprintf(log_path, sizeof(log_path), "build/wasm32_obj/%s.log", name);
  char full[1024];
  snprintf(full, sizeof(full), "%s > /dev/null 2> %s", cmd, log_path);
  int rc = system(full);
  if (rc == 0) {
    fprintf(stderr, "FAIL: expected failure for %s\n", name);
    return 1;
  }
  char buf[8192];
  if (slurp(log_path, buf, sizeof(buf)) != 0) return 1;
  if (!strstr(buf, needle)) {
    fprintf(stderr, "FAIL: %s missing diagnostic '%s'\n", name, needle);
    return 1;
  }
  return 0;
}

static int run_optional_link_case(void) {
  if (!command_available("wasm-ld") || !command_available("wasm-validate") ||
      !command_available("wasm-interp")) {
    return 0;
  }
  if (write_file("build/wasm32_obj/main.c",
                 "int other(void); int main(void){return other()+2;}\n") != 0 ||
      write_file("build/wasm32_obj/other.c",
                 "static int hidden(void){return 40;} int other(void){return hidden();}\n") != 0) {
    return 1;
  }
  if (run_cmd("./build/ag_c_wasm -c -o build/wasm32_obj/main.o build/wasm32_obj/main.c",
              "main.o") != 0) return 1;
  if (run_cmd("./build/ag_c_wasm -c -o build/wasm32_obj/other.o build/wasm32_obj/other.c",
              "other.o") != 0) return 1;
  if (run_cmd("wasm-ld --no-entry --export=main -o build/wasm32_obj/linked.wasm "
              "build/wasm32_obj/main.o build/wasm32_obj/other.o",
              "wasm-ld") != 0) return 1;
  if (run_cmd("wasm-validate build/wasm32_obj/linked.wasm", "wasm-validate") != 0) return 1;
  if (run_cmd("wasm-interp build/wasm32_obj/linked.wasm --run-all-exports "
              "> build/wasm32_obj/linked.interp",
              "wasm-interp") != 0) return 1;
  char buf[8192];
  if (slurp("build/wasm32_obj/linked.interp", buf, sizeof(buf)) != 0) return 1;
  if (!strstr(buf, "main() => i32:42")) {
    fprintf(stderr, "FAIL: linked wasm returned unexpected result\n");
    return 1;
  }
  return 0;
}

int main(void) {
  int failures = 0;
  if (mkdir("build/wasm32_obj", 0777) != 0) {
    /* already exists is fine; later file writes will report real failures */
  }

  const char *simple_needles[] = {
      "Custom:", "\"linking\"", "\"reloc.CODE\"", "<forty>", "R_WASM_FUNCTION_INDEX_LEB"};
  failures += run_objdump_check("simple",
                                "int forty(void){return 40;} int main(void){return forty()+2;}\n",
                                simple_needles, 5);

  const char *extern_needles[] = {"Import", "<other>", "undefined", "R_WASM_FUNCTION_INDEX_LEB"};
  failures += run_objdump_check("extern_call",
                                "int other(void); int main(void){return other();}\n",
                                extern_needles, 4);

  const char *extern_int_param_needles[] = {"<inc>", "undefined", "(i64) -> i32"};
  failures += run_objdump_check("extern_int_param",
                                "int inc(int); int main(void){return inc(4);}\n",
                                extern_int_param_needles, 3);

  const char *variadic_no_extra_needles[] = {"<pick>", "(i64) -> i32", "R_WASM_FUNCTION_INDEX_LEB"};
  failures += run_objdump_check("variadic_no_extra",
                                "int pick(int n, ...){return n;} int main(void){return pick(4);}\n",
                                variadic_no_extra_needles, 3);

  const char *extern_variadic_no_extra_needles[] = {"<log1>", "undefined", "(i32) -> i32"};
  failures += run_objdump_check("extern_variadic_no_extra",
                                "int log1(char *, ...); int main(void){return log1(\"x\");}\n",
                                extern_variadic_no_extra_needles, 3);

  const char *data_addr_needles[] = {
      "Data[1]", "<g>", "R_WASM_MEMORY_ADDR_LEB", "symbol=2 <g>"};
  failures += run_objdump_check("data_addr",
                                "int g=7; int *f(void){return &g;} int main(void){return 0;}\n",
                                data_addr_needles, 4);

  const char *string_addr_needles[] = {
      "Data[1]", "<.LC0>", "R_WASM_MEMORY_ADDR_LEB", "symbol=2 <.LC0>"};
  failures += run_objdump_check("string_addr",
                                "char *s(void){return \"hi\";} int main(void){return 0;}\n",
                                string_addr_needles, 4);

  const char *data_init_needles[] = {
      "\"reloc.DATA\"", "R_WASM_MEMORY_ADDR_I32", "symbol=2 <target>"};
  failures += run_objdump_check("data_init_reloc",
                                "int target=3; int *p=&target; int main(void){return 0;}\n",
                                data_init_needles, 3);

  const char *extern_data_needles[] = {
      "<ext>", "undefined", "R_WASM_MEMORY_ADDR_LEB", "symbol=1 <ext>"};
  failures += run_objdump_check("extern_data",
                                "extern int ext; int *f(void){return &ext;}\n",
                                extern_data_needles, 4);

  const char *global_read_needles[] = {
      "<g>", "R_WASM_MEMORY_ADDR_LEB", "i32.load", "symbol=1 <g>"};
  failures += run_objdump_check("global_read",
                                "int g=7; int main(void){return g;}\n",
                                global_read_needles, 4);

  const char *tls_global_read_needles[] = {
      "<tls>", "R_WASM_MEMORY_ADDR_LEB", "i32.load", "symbol=1 <tls>"};
  failures += run_objdump_check("tls_global_read",
                                "_Thread_local int tls=7; int main(void){return tls;}\n",
                                tls_global_read_needles, 4);

  const char *global_write_needles[] = {
      "<g>", "R_WASM_MEMORY_ADDR_LEB", "i32.store", "i32.load", "symbol=1 <g>"};
  failures += run_objdump_check("global_write",
                                "int g; int main(void){g=5; return g;}\n",
                                global_write_needles, 5);

  const char *global_fp_array_needles[] = {
      "<fa>", "0000 a03f 0000 3040", "<da>", "0000 0000 0000 f83f 0000 0000 0000 0440",
      "f32.load", "f64.load"};
  failures += run_objdump_check("global_fp_array",
                                "float fa[2]={1.25f,2.75f}; double da[2]={1.5,2.5}; "
                                "double f(void){return fa[0]+da[1];}\n",
                                global_fp_array_needles, 6);

  const char *static_multidim_needles[] = {
      "<f.a.", "binding=local", "size=24", "i32.store", "i32.load"};
  failures += run_objdump_check("static_multidim",
                                "int f(void){static int a[2][3]; "
                                "a[1][2]=a[1][2]+1; return a[1][2];} "
                                "int main(void){return f()*10+f();}\n",
                                static_multidim_needles, 5);

  const char *static_string_needles[] = {
      "<f.s.", "binding=local", "617a 00", "i32.store8", "i32.load8_s"};
  failures += run_objdump_check("static_string",
                                "int f(void){static char s[]=\"az\"; "
                                "s[0]=s[0]+1; return s[0]+s[1];} "
                                "int main(void){return f()*1000+f();}\n",
                                static_string_needles, 5);

  const char *local_stack_needles[] = {
      "__stack_pointer", "R_WASM_GLOBAL_INDEX_LEB", "global.set", "i32.store", "i32.load"};
  failures += run_objdump_check("local_stack",
                                "int main(void){int x=4; return x+1;}\n",
                                local_stack_needles, 5);

  const char *local_struct_needles[] = {
      "__stack_pointer", "R_WASM_GLOBAL_INDEX_LEB", "i32.add", "i32.store", "i32.load"};
  failures += run_objdump_check("local_struct",
                                "struct P{int x; int y;}; int main(void){"
                                "struct P a={1,2}; struct P b=a; return b.y;}\n",
                                local_struct_needles, 5);

  const char *local_struct_assign_needles[] = {"i64.load", "i64.store", "i32.load"};
  failures += run_objdump_check("local_struct_assign",
                                "struct P{int x; int y;}; int main(void){"
                                "struct P a={1,2}; struct P b; b=a; return b.y;}\n",
                                local_struct_assign_needles, 3);

  const char *large_struct_return_needles[] = {
      "(i32, i64) -> nil", "R_WASM_FUNCTION_INDEX_LEB", "i64.store", "i32.store"};
  failures += run_objdump_check("large_struct_return",
                                "struct P{int x; int y; int z;}; "
                                "struct P make(int x){struct P p={x,x+1,x+2}; return p;} "
                                "int main(void){struct P p=make(4); return p.z;}\n",
                                large_struct_return_needles, 4);

  const char *extern_large_struct_return_needles[] = {
      "<make>", "undefined", "(i32, i64) -> nil", "R_WASM_FUNCTION_INDEX_LEB"};
  failures += run_objdump_check("extern_large_struct_return",
                                "struct P{int x; int y; int z;}; struct P make(int); "
                                "int main(void){struct P p=make(4); return p.z;}\n",
                                extern_large_struct_return_needles, 4);

  const char *int_unary_needles[] = {"i32.sub"};
  failures += run_objdump_check("int_unary",
                                "int main(void){int x=5; return (-x)+(~x);}\n",
                                int_unary_needles, 1);

  const char *i64_shift_needles[] = {"i64.shl"};
  const char *i64_shift_rejects[] = {"i64.extend_i32_u"};
  failures += run_objdump_check_absent("i64_shift",
                                       "long f(long x){return x<<3;}\n",
                                       i64_shift_needles, 1, i64_shift_rejects, 1);

  const char *control_flow_needles[] = {"loop", "if", "br ", "unreachable"};
  failures += run_objdump_check("control_flow",
                                "int main(void){int x=0; if (x) return 1; return 2;}\n",
                                control_flow_needles, 4);

  const char *fp_local_needles[] = {"f64.const", "f64.store", "f64.load", "f64.add"};
  failures += run_objdump_check("fp_local",
                                "double f(void){double x=1.5; return x+2.0;}\n",
                                fp_local_needles, 4);

  const char *fp_convert_needles[] = {
      "f64.convert_i32_s", "i32.trunc_f64_s", "f32.demote_f64", "f64.promote_f32"};
  failures += run_objdump_check("fp_convert",
                                "double i2d(void){int x=3; return x;} "
                                "int d2i(double x){return (int)x;} "
                                "float d2f(double x){return (float)x;} "
                                "double f2d(float x){return x;}\n",
                                fp_convert_needles, 4);

  const char *complex_call_needles[] = {
      "(i32, f64, f64, f64, f64) -> nil", "R_WASM_FUNCTION_INDEX_LEB", "f64.store", "f64.load"};
  failures += run_objdump_check("complex_call",
                                "double _Complex zadd(double _Complex a,double _Complex b){return a+b;} "
                                "int main(void){double _Complex a={1.0,2.0}; "
                                "double _Complex b={3.0,4.0}; double _Complex z=zadd(a,b); "
                                "return (int)__real__ z;}\n",
                                complex_call_needles, 4);

  const char *align_ptr_needles[] = {"i32.add", "i32.const 31", "i32.const 4294967264", "i32.and"};
  failures += run_objdump_check("align_ptr",
                                "int main(void){_Alignas(32) int x=7; return x;}\n",
                                align_ptr_needles, 4);

  const char *vla_needles[] = {"__stack_pointer", "i32.const 15", "i32.and", "global.set",
                               "i32.store", "i32.load"};
  failures += run_objdump_check("vla_alloc",
                                "int main(void){int n=4; int a[n]; a[0]=7; return a[0];}\n",
                                vla_needles, 6);

  const char *va_arg_area_needles[] = {"__ag_va_arg_area", "R_WASM_GLOBAL_INDEX_LEB", "global.get"};
  failures += run_objdump_check("va_arg_area",
                                "#include <stdarg.h>\n"
                                "int first(int n, ...){va_list ap; va_start(ap,n); "
                                "return va_arg(ap,int);}\n",
                                va_arg_area_needles, 3);

  const char *atomic_needles[] = {"i32.load", "i32.store", "i32.add", "if", "nop"};
  failures += run_objdump_check("atomic_ops",
                                "#include <stdatomic.h>\n"
                                "atomic_int x; int expect;\n"
                                "int f(void){atomic_store(&x,3); "
                                "int old=atomic_fetch_add(&x,4); "
                                "int ok=atomic_compare_exchange_strong(&x,&expect,9); "
                                "atomic_thread_fence(memory_order_seq_cst); "
                                "return atomic_load(&x)+old+ok;}\n",
                                atomic_needles, 5);

  const char *atomic64_needles[] = {"i64.load", "i64.store", "i64.eq"};
  failures += run_objdump_check("atomic64_ops",
                                "#include <stdatomic.h>\n"
                                "atomic_llong x; long long expect;\n"
                                "long long f(void){atomic_store(&x,3); "
                                "long long old=atomic_exchange(&x,4); "
                                "int ok=atomic_compare_exchange_strong(&x,&expect,9); "
                                "return atomic_load(&x)+old+ok;}\n",
                                atomic64_needles, 3);

  const char *extern_global_read_needles[] = {
      "<ext>", "undefined", "R_WASM_MEMORY_ADDR_LEB", "i32.load", "symbol=1 <ext>"};
  failures += run_objdump_check("extern_global_read",
                                "extern int ext; int f(void){return ext;}\n",
                                extern_global_read_needles, 5);

  const char *extern_tls_read_needles[] = {
      "<ext>", "undefined", "R_WASM_MEMORY_ADDR_LEB", "i32.load", "symbol=1 <ext>"};
  failures += run_objdump_check("extern_tls_read",
                                "extern _Thread_local int ext; int f(void){return ext;}\n",
                                extern_tls_read_needles, 5);

  const char *extern_global_write_needles[] = {
      "<ext>", "undefined", "R_WASM_MEMORY_ADDR_LEB", "i32.store", "i32.load",
      "symbol=1 <ext>"};
  failures += run_objdump_check("extern_global_write",
                                "extern int ext; int f(void){ext=9; return ext;}\n",
                                extern_global_write_needles, 6);

  const char *struct_global_needles[] = {
      "Data[1]", "<pair>", "size=8", "0300 0000 0400 0000", "i32.load 2 0"};
  failures += run_objdump_check("struct_global",
                                "struct P{int x; int y;}; struct P pair={3,4}; "
                                "int main(void){return pair.y;}\n",
                                struct_global_needles, 5);

  const char *struct_array_needles[] = {
      "Data[1]", "<pts>", "size=16", "0100 0000 0200 0000 0300 0000 0400 0000",
      "i32.load 2 0"};
  failures += run_objdump_check("struct_array_global",
                                "struct P{int x; int y;}; struct P pts[2]={{1,2},{3,4}}; "
                                "int main(void){return pts[1].y;}\n",
                                struct_array_needles, 4);

  const char *nested_struct_needles[] = {
      "Data[1]", "<out>", "size=12", "0100 0000 0200 0000 0300 0000"};
  failures += run_objdump_check("nested_struct_global",
                                "struct In{int a; int b;}; struct Out{struct In in; int c;}; "
                                "struct Out out={{1,2},3}; int main(void){return out.c;}\n",
                                nested_struct_needles, 4);

  const char *union_global_needles[] = {
      "Data[1]", "<u>", "size=8", "0500 0000 0000 0000", "i32.load 2 0"};
  failures += run_objdump_check("union_global",
                                "union U{int i; double d;}; union U u={.i=5}; "
                                "int main(void){return u.i;}\n",
                                union_global_needles, 5);

  const char *union_fp_array_needles[] = {
      "Data[1]", "<g>", "0000 0000 0000 f43f 0000 0000 0000 0640", "f64.load",
      "i32.add"};
  const char *union_fp_array_rejects[] = {"i64.add"};
  failures += run_objdump_check_absent("union_fp_array",
                                       "union U{int i; double d;}; "
                                       "union U g[2]={{.d=1.25},{.d=2.75}}; "
                                       "int main(void){return (int)(g[0].d+g[1].d);}\n",
                                       union_fp_array_needles, 5,
                                       union_fp_array_rejects, 1);

  const char *bitfield_global_needles[] = {
      "Data[1]", "<s>", "size=4", "8d00 0000", "i32.shr_s", "i32.and"};
  failures += run_objdump_check("bitfield_global",
                                "struct S{unsigned a:3; unsigned b:5;}; struct S s={5,17}; "
                                "int main(void){return s.b;}\n",
                                bitfield_global_needles, 6);

  const char *struct_ptr_member_needles[] = {
      "\"reloc.DATA\"", "R_WASM_MEMORY_ADDR_I32", "symbol=2 <target>"};
  failures += run_objdump_check("struct_ptr_member",
                                "int target=5; struct Box{int *p;}; struct Box box={&target}; "
                                "int main(void){return 0;}\n",
                                struct_ptr_member_needles, 3);

  const char *func_addr_needles[] = {
      "R_WASM_TABLE_INDEX_SLEB", "<f>"};
  failures += run_objdump_check("func_addr",
                                "int f(void){return 1;} int (*addr(void))(void){return f;} "
                                "int main(void){return 0;}\n",
                                func_addr_needles, 2);

  const char *funcptr_global_needles[] = {
      "\"reloc.DATA\"", "R_WASM_TABLE_INDEX_I32", "<f>"};
  failures += run_objdump_check("funcptr_global",
                                "int f(void){return 1;} int (*p)(void)=f; "
                                "int main(void){return 0;}\n",
                                funcptr_global_needles, 3);

  const char *struct_funcptr_member_needles[] = {
      "\"reloc.DATA\"", "R_WASM_TABLE_INDEX_I32", "<f>", "<box>"};
  failures += run_objdump_check("struct_funcptr_member",
                                "int f(void){return 1;} struct Box{int (*p)(void);}; "
                                "struct Box box={f}; int main(void){return 0;}\n",
                                struct_funcptr_member_needles, 4);

  const char *indirect_needles[] = {
      "__indirect_function_table", "R_WASM_TABLE_INDEX_I32", "call_indirect"};
  failures += run_objdump_check("indirect",
                                "int one(void){return 1;} int (*p)(void)=one; "
                                "int main(void){return p();}\n",
                                indirect_needles, 3);

  const char *local_indirect_needles[] = {
      "__indirect_function_table", "__stack_pointer", "R_WASM_TABLE_INDEX_SLEB",
      "R_WASM_GLOBAL_INDEX_LEB", "call_indirect"};
  failures += run_objdump_check("local_indirect",
                                "int one(void){return 1;} "
                                "int main(void){int (*p)(void)=one; return p();}\n",
                                local_indirect_needles, 5);

  const char *static_needles[] = {"<hidden>", "binding=local", "<main>"};
  failures += run_objdump_check("static_func",
                                "static int hidden(void){return 7;} int main(void){return hidden();}\n",
                                static_needles, 3);

  failures += run_fail_case("missing_o", "./build/ag_c_wasm -c build/wasm32_obj/simple.c",
                            "E0002");

  if (write_file("build/wasm32_obj/variadic_extra_reject.c",
                 "int pick(int n, ...){return n;} int main(void){return pick(1,2);}\n") != 0) {
    fprintf(stderr, "FAIL: write variadic_extra_reject.c\n");
    failures++;
  } else {
    failures += run_fail_case("variadic_extra_reject",
                              "./build/ag_c_wasm -c -o build/wasm32_obj/variadic_extra_reject.o "
                              "build/wasm32_obj/variadic_extra_reject.c",
                              "E4008");
  }

  failures += run_optional_link_case();

  if (failures) {
    fprintf(stderr, "wasm32 object tests failed: %d\n", failures);
    return 1;
  }
  printf("wasm32 object tests passed\n");
  return 0;
}
