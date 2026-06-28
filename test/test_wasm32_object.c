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

  const char *global_write_needles[] = {
      "<g>", "R_WASM_MEMORY_ADDR_LEB", "i32.store", "i32.load", "symbol=1 <g>"};
  failures += run_objdump_check("global_write",
                                "int g; int main(void){g=5; return g;}\n",
                                global_write_needles, 5);

  const char *extern_global_read_needles[] = {
      "<ext>", "undefined", "R_WASM_MEMORY_ADDR_LEB", "i32.load", "symbol=1 <ext>"};
  failures += run_objdump_check("extern_global_read",
                                "extern int ext; int f(void){return ext;}\n",
                                extern_global_read_needles, 5);

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

  const char *struct_ptr_member_needles[] = {
      "\"reloc.DATA\"", "R_WASM_MEMORY_ADDR_I32", "symbol=2 <target>"};
  failures += run_objdump_check("struct_ptr_member",
                                "int target=5; struct Box{int *p;}; struct Box box={&target}; "
                                "int main(void){return 0;}\n",
                                struct_ptr_member_needles, 3);

  const char *static_needles[] = {"<hidden>", "binding=local", "<main>"};
  failures += run_objdump_check("static_func",
                                "static int hidden(void){return 7;} int main(void){return hidden();}\n",
                                static_needles, 3);

  failures += run_fail_case("missing_o", "./build/ag_c_wasm -c build/wasm32_obj/simple.c",
                            "E0002");
  if (write_file("build/wasm32_obj/indirect.c",
                 "int one(void){return 1;} int main(void){int (*p)(void)=one; return p();}\n") == 0) {
    failures += run_fail_case("indirect",
                              "./build/ag_c_wasm -c -o build/wasm32_obj/indirect.o "
                              "build/wasm32_obj/indirect.c",
                              "E4008");
  } else {
    failures++;
  }

  failures += run_optional_link_case();

  if (failures) {
    fprintf(stderr, "wasm32 object tests failed: %d\n", failures);
    return 1;
  }
  printf("wasm32 object tests passed\n");
  return 0;
}
