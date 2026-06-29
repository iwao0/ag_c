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
  if (command_available("wasm-validate")) {
    snprintf(cmd, sizeof(cmd), "wasm-validate %s", o_path);
    if (run_cmd(cmd, "wasm-validate object") != 0) return 1;
  }

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
  if (command_available("wasm-validate")) {
    snprintf(cmd, sizeof(cmd), "wasm-validate %s", o_path);
    if (run_cmd(cmd, "wasm-validate object") != 0) return 1;
  }

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
  if (write_file("build/wasm32_obj/global_main.c",
                 "extern int shared; int bump(void); int main(void){shared=37; return bump();}\n") != 0 ||
      write_file("build/wasm32_obj/global_other.c",
                 "int shared; int bump(void){shared=shared+5; return shared;}\n") != 0) {
    return 1;
  }
  if (run_cmd("./build/ag_c_wasm -c -o build/wasm32_obj/global_main.o "
              "build/wasm32_obj/global_main.c",
              "global_main.o") != 0) return 1;
  if (run_cmd("./build/ag_c_wasm -c -o build/wasm32_obj/global_other.o "
              "build/wasm32_obj/global_other.c",
              "global_other.o") != 0) return 1;
  if (run_cmd("wasm-ld --no-entry --export=main -o build/wasm32_obj/linked_global.wasm "
              "build/wasm32_obj/global_main.o build/wasm32_obj/global_other.o",
              "wasm-ld global") != 0) return 1;
  if (run_cmd("wasm-validate build/wasm32_obj/linked_global.wasm",
              "wasm-validate global") != 0) return 1;
  if (run_cmd("wasm-interp build/wasm32_obj/linked_global.wasm --run-all-exports "
              "> build/wasm32_obj/linked_global.interp",
              "wasm-interp global") != 0) return 1;
  if (slurp("build/wasm32_obj/linked_global.interp", buf, sizeof(buf)) != 0) return 1;
  if (!strstr(buf, "main() => i32:42")) {
    fprintf(stderr, "FAIL: linked global wasm returned unexpected result\n");
    return 1;
  }
  if (write_file("build/wasm32_obj/static_main.c",
                 "int a(void); int b(void); int main(void){return a()+b();}\n") != 0 ||
      write_file("build/wasm32_obj/static_a.c",
                 "static int hidden(void){return 20;} int a(void){return hidden();}\n") != 0 ||
      write_file("build/wasm32_obj/static_b.c",
                 "static int hidden(void){return 22;} int b(void){return hidden();}\n") != 0) {
    return 1;
  }
  if (run_cmd("./build/ag_c_wasm -c -o build/wasm32_obj/static_main.o "
              "build/wasm32_obj/static_main.c",
              "static_main.o") != 0) return 1;
  if (run_cmd("./build/ag_c_wasm -c -o build/wasm32_obj/static_a.o "
              "build/wasm32_obj/static_a.c",
              "static_a.o") != 0) return 1;
  if (run_cmd("./build/ag_c_wasm -c -o build/wasm32_obj/static_b.o "
              "build/wasm32_obj/static_b.c",
              "static_b.o") != 0) return 1;
  if (run_cmd("wasm-ld --no-entry --export=main -o build/wasm32_obj/linked_static.wasm "
              "build/wasm32_obj/static_main.o build/wasm32_obj/static_a.o "
              "build/wasm32_obj/static_b.o",
              "wasm-ld static") != 0) return 1;
  if (run_cmd("wasm-validate build/wasm32_obj/linked_static.wasm",
              "wasm-validate static") != 0) return 1;
  if (run_cmd("wasm-interp build/wasm32_obj/linked_static.wasm --run-all-exports "
              "> build/wasm32_obj/linked_static.interp",
              "wasm-interp static") != 0) return 1;
  if (slurp("build/wasm32_obj/linked_static.interp", buf, sizeof(buf)) != 0) return 1;
  if (!strstr(buf, "main() => i32:42")) {
    fprintf(stderr, "FAIL: linked static wasm returned unexpected result\n");
    return 1;
  }
  return 0;
}

int main(void) {
  int failures = 0;
  if (mkdir("build/wasm32_obj", 0777) != 0) {
    /* already exists is fine; later file writes will report real failures */
  }

  const char *simple_needles[] = {"Custom:",
                                  "\"linking\"",
                                  "\"reloc.CODE\"",
                                  "__linear_memory",
                                  "<forty>",
                                  "R_WASM_FUNCTION_INDEX_LEB"};
  failures += run_objdump_check("simple",
                                "int forty(void){return 40;} int main(void){return forty()+2;}\n",
                                simple_needles, 6);

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

  const char *variadic_extra_needles[] = {
      "__ag_va_arg_area", "__stack_pointer", "(i64) -> i32", "i64.store",
      "R_WASM_FUNCTION_INDEX_LEB"};
  failures += run_objdump_check("variadic_extra",
                                "int pick(int n, ...){return n;} "
                                "int main(void){return pick(1,2);}\n",
                                variadic_extra_needles, 5);

  const char *variadic_va_arg_read_needles[] = {
      "__ag_va_arg_area", "__stack_pointer", "(i64) -> i32", "i64.store", "i64.load"};
  failures += run_objdump_check("variadic_va_arg_read",
                                "#include <stdarg.h>\n"
                                "int first(int n, ...){va_list ap; va_start(ap,n); "
                                "return va_arg(ap,int);} "
                                "int main(void){return first(0,42);}\n",
                                variadic_va_arg_read_needles, 5);

  const char *variadic_fp_extra_needles[] = {
      "__ag_va_arg_area", "(i64) -> i32", "f64.promote_f32", "f64.store"};
  failures += run_objdump_check("variadic_fp_extra",
                                "int pick(int n, ...){return n;} "
                                "int main(void){float x=1.5f; return pick(1,x);}\n",
                                variadic_fp_extra_needles, 4);

  const char *extern_variadic_extra_needles[] = {
      "<log1>", "undefined", "__ag_va_arg_area", "(i32) -> i32", "i64.store"};
  failures += run_objdump_check("extern_variadic_extra",
                                "int log1(char *, ...); int main(void){return log1(\"x\",42);}\n",
                                extern_variadic_extra_needles, 5);

  const char *indirect_variadic_extra_needles[] = {
      "__indirect_function_table", "__ag_va_arg_area", "(i64) -> i32", "call_indirect",
      "i64.store"};
  failures += run_objdump_check("local_variadic_funcptr_extra",
                                "int pick(int n, ...){return n;} "
                                "int main(void){int (*fp)(int,...)=pick; return fp(1,2);}\n",
                                indirect_variadic_extra_needles, 5);

  failures += run_objdump_check("global_variadic_funcptr_extra",
                                "int pick(int n, ...){return n;} int (*gfp)(int,...)=pick; "
                                "int main(void){return gfp(1,2);}\n",
                                indirect_variadic_extra_needles, 5);
  failures += run_objdump_check("struct_variadic_funcptr_extra",
                                "int pick(int n, ...){return n;} "
                                "struct Ops{int (*f)(int,...);}; struct Ops ops={pick}; "
                                "int main(void){return ops.f(1,2);}\n",
                                indirect_variadic_extra_needles, 5);
  failures += run_objdump_check("typedef_struct_variadic_funcptr_extra",
                                "typedef int (*VOp)(int,...); int pick(int n, ...){return n;} "
                                "struct Ops{VOp f;}; struct Ops ops={pick}; "
                                "int main(void){return ops.f(1,2);}\n",
                                indirect_variadic_extra_needles, 5);
  failures += run_objdump_check("typedef_local_variadic_funcptr_extra",
                                "typedef int (*VOp)(int,...); int pick(int n, ...){return n;} "
                                "int main(void){VOp fp=pick; return fp(1,2);}\n",
                                indirect_variadic_extra_needles, 5);

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
  const char *string_ucn_needles[] = {
      "Data[1]", "<.LC0>", "e381 8200", "R_WASM_MEMORY_ADDR_LEB"};
  failures += run_objdump_check("string_ucn_addr",
                                "char *s(void){return \"\\u3042\";} int main(void){return s()[0];}\n",
                                string_ucn_needles, 4);
  const char *wide_string_u16_needles[] = {
      "Data[1]", "<.LC0>", "4100 5a00 0000", "R_WASM_MEMORY_ADDR_LEB"};
  failures += run_objdump_check("wide_string_u16_addr",
                                "unsigned short *s(void){return u\"AZ\";} "
                                "int main(void){return s()[1];}\n",
                                wide_string_u16_needles, 4);
  const char *wide_string_u32_needles[] = {
      "Data[1]", "<.LC0>", "4100 0000 5a00 0000 0000 0000", "R_WASM_MEMORY_ADDR_LEB"};
  failures += run_objdump_check("wide_string_u32_addr",
                                "unsigned int *s(void){return U\"AZ\";} "
                                "int main(void){return s()[1];}\n",
                                wide_string_u32_needles, 4);

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

  const char *i64_shift_needles[] = {"i64.extend_i32_u", "i64.shl"};
  failures += run_objdump_check("i64_shift",
                                "long f(long x){return x<<3;}\n",
                                i64_shift_needles, 2);

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

  const char *fp_unsigned_convert_needles[] = {
      "f64.convert_i32_u", "i32.trunc_f64_u", "f64.convert_i64_u", "i64.trunc_f64_u"};
  failures += run_objdump_check("fp_unsigned_convert",
                                "int u32_to_d(void){unsigned int x; x=4294967295U; "
                                "double d; d=x; return d>4294967294.0;} "
                                "int d_to_u32(void){double d; d=4294967295.0; "
                                "unsigned int x; x=d; return x==4294967295U;} "
                                "int u64_to_d(void){unsigned long x; x=4294967296UL; "
                                "double d; d=x; return d>4294967295.0;} "
                                "int d_to_u64(void){double d; d=4294967296.0; "
                                "unsigned long x; x=d; return x==4294967296UL;}\n",
                                fp_unsigned_convert_needles, 4);

  const char *fp_compare_neg_needles[] = {"f64.lt", "f64.neg", "f32.neg"};
  failures += run_objdump_check("fp_compare_neg",
                                "int dcmp(void){double x; x=2.0; return x<3.0;} "
                                "int dneg(void){double x; x=-2.0; return (int)(-x);} "
                                "int fneg(void){float x; x=3.5f; return (int)(-x);}\n",
                                fp_compare_neg_needles, 3);

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

  const char *struct_fp_array_member_needles[] = {
      "Data[1]", "<r>", "0000 0000 0000 f83f 0000 0000 0000 0440",
      "0000 0000 0000 0c40 0000 0000 0000 1240", "f64.load"};
  failures += run_objdump_check("struct_fp_array_member",
                                "struct R{double m[2][2];}; "
                                "struct R r={{{1.5,2.5},{3.5,4.5}}}; "
                                "int main(void){return (int)r.m[1][1];}\n",
                                struct_fp_array_member_needles, 5);

  const char *struct_union_fp_needles[] = {
      "Data[1]", "<g>", "0000 0000 0000 0440 0400 0000", "f64.load", "i32.load"};
  failures += run_objdump_check("struct_union_fp",
                                "union U{int i; double d;}; struct S{union U u; int x;}; "
                                "struct S g={{.d=2.5},4}; "
                                "int main(void){return (int)g.u.d+g.x;}\n",
                                struct_union_fp_needles, 5);

  const char *union_struct_fp_needles[] = {
      "Data[1]", "<g>", "0000 0000 0000 0440 0400 0000", "f64.load", "i32.load"};
  failures += run_objdump_check("union_struct_fp",
                                "struct S{double d; int x;}; union U{struct S s; long raw;}; "
                                "union U g={.s={2.5,4}}; "
                                "int main(void){return (int)g.s.d+g.s.x;}\n",
                                union_struct_fp_needles, 5);

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

  const char *extern_funcptr_global_needles[] = {
      "<fprintf>", "undefined", "(i32, i32) -> i32", "R_WASM_TABLE_INDEX_I32", "call_indirect"};
  const char *extern_funcptr_rejects[] = {"(i64, i64) -> i32"};
  failures += run_objdump_check_absent("extern_funcptr_global",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "int (*p)(FILE*, const char*, ...) = &fprintf; "
                                       "int main(void){return p(stdout, \"x\");}\n",
                                       extern_funcptr_global_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_global_cast",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "Printer p=(Printer)&fprintf; "
                                       "int main(void){return p(stdout, \"x\");}\n",
                                       extern_funcptr_global_needles, 5,
                                       extern_funcptr_rejects, 1);

  const char *extern_funcptr_array_needles[] = {
      "<fprintf>", "undefined", "(i32, i32) -> i32", "R_WASM_TABLE_INDEX_I32",
      "call_indirect"};
  failures += run_objdump_check_absent("extern_funcptr_array",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "int (*ops[1])(FILE*, const char*, ...)={&fprintf}; "
                                       "int main(void){return ops[0](stdout, \"x\");}\n",
                                       extern_funcptr_array_needles, 5,
                                       extern_funcptr_rejects, 1);

  const char *extern_local_funcptr_needles[] = {
      "<fprintf>", "undefined", "(i32, i32) -> i32", "R_WASM_TABLE_INDEX_SLEB",
      "call_indirect"};
  failures += run_objdump_check_absent("extern_local_funcptr",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "int main(void){int (*p)(FILE*, const char*, ...)=&fprintf; "
                                       "return p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_local_funcptr_assign",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "int main(void){int (*p)(FILE*, const char*, ...); "
                                       "p=&fprintf; return p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_typedef_local_funcptr",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "int main(void){Printer p=&fprintf; "
                                       "return p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_typedef_cast_funcptr",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "int main(void){Printer p=(Printer)&fprintf; "
                                       "return p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_local_funcptr_array_assign",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "int main(void){Printer ops[1]; ops[0]=&fprintf; "
                                       "return ops[0](stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_return",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "Printer get(void){return &fprintf;} "
                                       "int main(void){return get()(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_return_ternary",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "Printer get(int x){return x ? &fprintf : &fprintf;} "
                                       "int main(void){return get(1)(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_return_comma",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "Printer get(int x){return x, &fprintf;} "
                                       "int main(void){return get(1)(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_return_direct_decl",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "int (*get(void))(FILE*, const char*, ...){return &fprintf;} "
                                       "int main(void){return get()(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_return_stmt_expr",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "Printer get(void){return ({ &fprintf; });} "
                                       "int main(void){return get()(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_return_cast",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "Printer get(void){return (Printer)&fprintf;} "
                                       "int main(void){return get()(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_funcptr_return_store_local",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "Printer get(void){return &fprintf;} "
                                       "int main(void){Printer p=get(); return p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  const char *struct_funcptr_member_needles[] = {
      "\"reloc.DATA\"", "R_WASM_TABLE_INDEX_I32", "<f>", "<box>"};
  failures += run_objdump_check("struct_funcptr_member",
                                "int f(void){return 1;} struct Box{int (*p)(void);}; "
                                "struct Box box={f}; int main(void){return 0;}\n",
                                struct_funcptr_member_needles, 4);

  const char *extern_struct_funcptr_member_needles[] = {
      "<fprintf>", "undefined", "(i32, i32) -> i32", "R_WASM_TABLE_INDEX_I32",
      "call_indirect", "<ops>"};
  failures += run_objdump_check_absent("extern_struct_funcptr_member",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "struct Ops{int (*p)(FILE*, const char*, ...);}; "
                                       "struct Ops ops={&fprintf}; "
                                       "int main(void){return ops.p(stdout, \"x\");}\n",
                                       extern_struct_funcptr_member_needles, 6,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_local_struct_funcptr_member",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "struct Ops{int (*p)(FILE*, const char*, ...);}; "
                                       "int main(void){struct Ops ops; ops.p=&fprintf; "
                                       "return ops.p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_local_struct_funcptr_cast",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "typedef int (*Printer)(FILE*, const char*, ...); "
                                       "struct Ops{Printer p;}; "
                                       "int main(void){struct Ops ops; ops.p=(Printer)&fprintf; "
                                       "return ops.p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

  failures += run_objdump_check_absent("extern_local_struct_funcptr_arrow",
                                       "typedef struct FILE FILE; extern FILE *stdout; "
                                       "int fprintf(FILE*, const char*, ...); "
                                       "struct Ops{int (*p)(FILE*, const char*, ...);}; "
                                       "int main(void){struct Ops ops; struct Ops *q=&ops; "
                                       "q->p=&fprintf; return q->p(stdout, \"x\");}\n",
                                       extern_local_funcptr_needles, 5,
                                       extern_funcptr_rejects, 1);

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

  const char *complex_funcptr_assign_needles[] = {
      "R_WASM_TABLE_INDEX_SLEB", "<zadd>", "i32.store"};
  failures += run_objdump_check("complex_funcptr_assign",
                                "double _Complex zadd(double _Complex a,double _Complex b){return a+b;} "
                                "int main(void){double _Complex (*fp)(double _Complex,double _Complex); "
                                "fp=zadd; return fp!=0;}\n",
                                complex_funcptr_assign_needles, 3);

  const char *indirect_complex_return_needles[] = {
      "__indirect_function_table", "(i32, f64, f64, f64, f64) -> nil",
      "call_indirect", "f64.store"};
  failures += run_objdump_check("indirect_complex_return",
                                "double _Complex zadd(double _Complex a,double _Complex b){return a+b;} "
                                "int main(void){double _Complex (*fp)(double _Complex,double _Complex); "
                                "fp=zadd; double _Complex a={1.0,2.0}; double _Complex b={3.0,4.0}; "
                                "double _Complex z=fp(a,b); return (int)__real__ z;}\n",
                                indirect_complex_return_needles, 4);

  failures += run_objdump_check("typedef_complex_funcptr_return",
                                "typedef double _Complex (*Zop)(double _Complex,double _Complex); "
                                "double _Complex zadd(double _Complex a,double _Complex b){return a+b;} "
                                "int main(void){Zop fp=zadd; double _Complex a={1.0,2.0}; "
                                "double _Complex b={3.0,4.0}; double _Complex z=fp(a,b); "
                                "return (int)__real__ z;}\n",
                                indirect_complex_return_needles, 4);

  failures += run_objdump_check("global_complex_funcptr_return",
                                "double _Complex zadd(double _Complex a,double _Complex b){return a+b;} "
                                "double _Complex (*gfp)(double _Complex,double _Complex)=zadd; "
                                "int main(void){double _Complex a={1.0,2.0}; double _Complex b={3.0,4.0}; "
                                "double _Complex z=gfp(a,b); return (int)__real__ z;}\n",
                                indirect_complex_return_needles, 4);

  failures += run_objdump_check("struct_complex_funcptr_return",
                                "double _Complex zadd(double _Complex a,double _Complex b){return a+b;} "
                                "struct Ops{double _Complex (*f)(double _Complex,double _Complex);}; "
                                "struct Ops ops={zadd}; int main(void){double _Complex a={1.0,2.0}; "
                                "double _Complex b={3.0,4.0}; double _Complex z=ops.f(a,b); "
                                "return (int)__real__ z;}\n",
                                indirect_complex_return_needles, 4);

  const char *indirect_double_needles[] = {
      "__indirect_function_table", "(f64, f64) -> f64", "f64.add", "call_indirect"};
  failures += run_objdump_check("indirect_double",
                                "double addd(double x,double y){return x+y;} "
                                "int main(void){double (*fp)(double,double)=addd; "
                                "return (int)fp(1.25,2.75);}\n",
                                indirect_double_needles, 4);

  const char *indirect_int_to_double_arg_needles[] = {
      "__indirect_function_table", "(f64) -> f64", "f64.convert_i32_s", "call_indirect"};
  failures += run_objdump_check("indirect_int_to_double_arg",
                                "double add(double x){return x+0.5;} "
                                "int main(void){double (*fp)(double)=add; return (int)fp(3);}\n",
                                indirect_int_to_double_arg_needles, 4);

  const char *indirect_double_to_int_arg_needles[] = {
      "__indirect_function_table", "(i64) -> i32", "trunc_f64_s", "call_indirect"};
  failures += run_objdump_check("indirect_double_to_int_arg",
                                "int take(int x){return x;} "
                                "int main(void){int (*fp)(int)=take; return fp(7.9);}\n",
                                indirect_double_to_int_arg_needles, 4);

  const char *indirect_pointer_return_needles[] = {
      "__indirect_function_table", "() -> i32", "R_WASM_TABLE_INDEX_SLEB", "i32.store"};
  failures += run_objdump_check("indirect_pointer_return",
                                "int g; int *get(void){return &g;} "
                                "int main(void){int *(*fp)(void)=get; *fp()=42; return g;}\n",
                                indirect_pointer_return_needles, 4);

  const char *indirect_large_struct_return_needles[] = {
      "__indirect_function_table", "(i32, i64) -> nil", "call_indirect", "i64.store"};
  failures += run_objdump_check("indirect_large_struct_return",
                                "struct Big{int a; int b; int c;}; "
                                "struct Big mkbig(int x){struct Big r; r.a=x; r.b=x+1; "
                                "r.c=x+2; return r;} "
                                "int main(void){struct Big (*fp)(int)=mkbig; "
                                "struct Big r=fp(40); return r.a+r.c;}\n",
                                indirect_large_struct_return_needles, 4);

  const char *global_funcptr_array_call_needles[] = {
      "__indirect_function_table", "R_WASM_TABLE_INDEX_I32", "call_indirect", "i32.load"};
  failures += run_objdump_check("global_funcptr_array_call",
                                "int add1(int x){return x+1;} int add2(int x){return x+2;} "
                                "int (*ops[2])(int)={add1,add2}; int main(void){return ops[1](40);}\n",
                                global_funcptr_array_call_needles, 4);

  const char *struct_funcptr_offset_call_needles[] = {
      "__indirect_function_table", "R_WASM_TABLE_INDEX_I32", "call_indirect", "<ops>"};
  failures += run_objdump_check("struct_funcptr_offset_call",
                                "int add1(int x){return x+1;} "
                                "struct Ops{int pad; int (*f)(int);}; struct Ops ops={5,add1}; "
                                "int main(void){return ops.f(41);}\n",
                                struct_funcptr_offset_call_needles, 4);

  const char *static_needles[] = {"<hidden>", "binding=local", "<main>"};
  failures += run_objdump_check("static_func",
                                "static int hidden(void){return 7;} int main(void){return hidden();}\n",
                                static_needles, 3);

  failures += run_fail_case("missing_o", "./build/ag_c_wasm -c build/wasm32_obj/simple.c",
                            "E0002");

  failures += run_optional_link_case();

  if (failures) {
    fprintf(stderr, "wasm32 object tests failed: %d\n", failures);
    return 1;
  }
  printf("wasm32 object tests passed\n");
  return 0;
}
