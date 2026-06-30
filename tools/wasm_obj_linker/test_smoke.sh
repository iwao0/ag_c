#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
out_dir="$root/build/ag_wasm_link_smoke"

if ! command -v wasm-validate >/dev/null 2>&1 || ! command -v wasm-interp >/dev/null 2>&1; then
  echo "ag_wasm_link smoke: skip (wasm-validate/wasm-interp not found)"
  exit 0
fi

mkdir -p "$out_dir"

cat > "$out_dir/main.c" <<'SRC'
extern int other(int);
int main(void) { return other(40) + 2; }
SRC

cat > "$out_dir/other.c" <<'SRC'
int other(int x) { return x; }
SRC

cat > "$out_dir/gmain.c" <<'SRC'
extern int g;
int main(void) {
  g = g + 5;
  return g;
}
SRC

cat > "$out_dir/gother.c" <<'SRC'
int g = 37;
SRC

cat > "$out_dir/addr_main.c" <<'SRC'
extern int *p;
int main(void) { return *p + 2; }
SRC

cat > "$out_dir/addr_other.c" <<'SRC'
int g = 40;
int *p = &g;
SRC

cat > "$out_dir/static_main.c" <<'SRC'
int a(void);
int b(void);
int main(void) { return a() + b(); }
SRC

cat > "$out_dir/static_a.c" <<'SRC'
static int same(void) { return 20; }
int a(void) { return same(); }
SRC

cat > "$out_dir/static_b.c" <<'SRC'
static int same(void) { return 22; }
int b(void) { return same(); }
SRC

cat > "$out_dir/import_main.c" <<'SRC'
extern int host_add(int);
int main(void) { return host_add(41); }
SRC

cat > "$out_dir/fp_cross_main.c" <<'SRC'
int add1(int);
int main(void) {
  int (*p)(int) = add1;
  return p(41);
}
SRC

cat > "$out_dir/fp_cross_other.c" <<'SRC'
int add1(int x) { return x + 1; }
SRC

cat > "$out_dir/fp_data_single.c" <<'SRC'
int add1(int x) { return x + 1; }
int (*p)(int) = add1;
int main(void) { return p(41); }
SRC

cat > "$out_dir/fp_import.c" <<'SRC'
extern int host_add(int);
int main(void) {
  int (*p)(int) = host_add;
  return p(41);
}
SRC

cat > "$out_dir/indirect_no_elem.c" <<'SRC'
typedef int (*fptr)(int);
int call_it(fptr fp, int x) { return fp(x); }
int main(void) { return 0; }
SRC

cat > "$out_dir/fp_data_import.c" <<'SRC'
extern int host_add(int);
int (*p)(int) = host_add;
int main(void) { return p(41); }
SRC

cat > "$out_dir/fp_data_cross_main.c" <<'SRC'
extern int (*p)(int);
int main(void) { return p(41); }
SRC

cat > "$out_dir/fp_data_cross_other.c" <<'SRC'
int add1(int x) { return x + 1; }
int (*p)(int) = add1;
SRC

cat > "$out_dir/bss_big.c" <<'SRC'
char big[70000];
int main(void) {
  big[69999] = 7;
  return big[69999] + 35;
}
SRC

cat > "$out_dir/bss_overflow.c" <<'SRC'
char a[2147483000];
char b[2147483000];
int main(void) { return 0; }
SRC

cat > "$out_dir/data_symbol_offset_main.c" <<'SRC'
extern int alias;
int main(void) { return alias; }
SRC

cat > "$out_dir/data_symbol_offset_other.c" <<'SRC'
int alias[2] = {40, 42};
SRC

cat > "$out_dir/dup_func_main.c" <<'SRC'
int dup(void);
int main(void) { return dup(); }
SRC

cat > "$out_dir/dup_func_a.c" <<'SRC'
int dup(void) { return 40; }
SRC

cat > "$out_dir/dup_func_b.c" <<'SRC'
int dup(void) { return 42; }
SRC

cat > "$out_dir/dup_data_main.c" <<'SRC'
extern int dup_data;
int main(void) { return dup_data; }
SRC

cat > "$out_dir/dup_data_a.c" <<'SRC'
int dup_data = 40;
SRC

cat > "$out_dir/dup_data_b.c" <<'SRC'
int dup_data = 42;
SRC

cat > "$out_dir/sig_mismatch_main.c" <<'SRC'
extern int sig_mismatch(int);
int main(void) { return sig_mismatch(42); }
SRC

cat > "$out_dir/sig_mismatch_other.c" <<'SRC'
int sig_mismatch(double x) { return (int)x; }
SRC

cat > "$out_dir/import_sig_a.c" <<'SRC'
extern int host_mix(int);
int a(void) { return host_mix(1); }
SRC

cat > "$out_dir/import_sig_b.c" <<'SRC'
extern int host_mix(double);
int b(void) { return host_mix(1.0); }
SRC

cat > "$out_dir/snprintf_negative.c" <<'SRC'
int snprintf(char *s, unsigned long n, const char *fmt, ...);
int sprintf(char *s, const char *fmt, ...);
int main(void) {
  char a[32];
  char b[32];
  char c[32];
  char d[32];
  char e[32];
  char f[32];
  char g[32];
  char h[32];
  char i[32];
  char j[32];
  char k[32];
  char l[32];
  char m[32];
  char n[32];
  int na = snprintf(a, sizeof(a), "%d", -42);
  int nb = snprintf(b, sizeof(b), "%d-%d", -12, 34);
  int nc = snprintf(c, sizeof(c), "%u", 4294967295u);
  int nd = snprintf(d, sizeof(d), "%02d", 7);
  int ne = snprintf(e, sizeof(e), "%02d", -5);
  int nf = sprintf(f, "%d", -42);
  int ng = sprintf(g, "%u", 4294967295u);
  int nh = sprintf(h, "%02d", 7);
  int ni = snprintf(i, sizeof(i), "%s", "wasm");
  int nj = sprintf(j, "%s", "link");
  int nk = snprintf(k, sizeof(k), "%c", 'Z');
  int nl = sprintf(l, "%c", 'Q');
  int nm = snprintf(m, sizeof(m), "%%");
  int nn = sprintf(n, "%%");
  return na == 3 && a[0] == '-' && a[1] == '4' && a[2] == '2' && a[3] == 0 &&
         nb == 6 && b[0] == '-' && b[1] == '1' && b[2] == '2' && b[3] == '-' &&
         b[4] == '3' && b[5] == '4' && b[6] == 0 &&
         nc == 10 && c[0] == '4' && c[1] == '2' && c[2] == '9' && c[3] == '4' &&
         c[4] == '9' && c[5] == '6' && c[6] == '7' && c[7] == '2' &&
         c[8] == '9' && c[9] == '5' && c[10] == 0 &&
         nd == 2 && d[0] == '0' && d[1] == '7' && d[2] == 0 &&
         ne == 2 && e[0] == '-' && e[1] == '5' && e[2] == 0 &&
         nf == 3 && f[0] == '-' && f[1] == '4' && f[2] == '2' && f[3] == 0 &&
         ng == 10 && g[0] == '4' && g[9] == '5' && g[10] == 0 &&
         nh == 2 && h[0] == '0' && h[1] == '7' && h[2] == 0 &&
         ni == 4 && i[0] == 'w' && i[1] == 'a' && i[2] == 's' && i[3] == 'm' && i[4] == 0 &&
         nj == 4 && j[0] == 'l' && j[1] == 'i' && j[2] == 'n' && j[3] == 'k' && j[4] == 0 &&
         nk == 1 && k[0] == 'Z' && k[1] == 0 &&
         nl == 1 && l[0] == 'Q' && l[1] == 0 &&
         nm == 1 && m[0] == '%' && m[1] == 0 &&
         nn == 1 && n[0] == '%' && n[1] == 0 ? 42 : 1;
}
SRC

cat > "$out_dir/libc_runtime.c" <<'SRC'
long strlen(char *s);
int strcmp(char *a, char *b);
void *memset(void *s, int c, unsigned long n);
void *memcpy(void *dst, void *src, unsigned long n);
int abs(int x);
long imaxabs(long x);
int isdigit(int c);
int isalpha(int c);
int toupper(int c);
int iswdigit(int c);
int iswalpha(int c);
int towupper(int c);
void *malloc(long size);
void *calloc(long nmemb, long size);
void free(void *p);
int feclearexcept(int excepts);
int fetestexcept(int excepts);
char *setlocale(int category, char *locale);
struct lconv { char *decimal_point; };
struct lconv *localeconv(void);
double sqrt(double x);
float sqrtf(float x);
double pow(double x, double y);
double fabs(double x);
int atoi(char *s);
char *strcpy(char *dst, char *src);
char *strncpy(char *dst, char *src, unsigned long n);
char *strcat(char *dst, char *src);
int strncmp(char *a, char *b, unsigned long n);
int memcmp(void *a, void *b, unsigned long n);
char *strchr(char *s, int ch);
char *strrchr(char *s, int ch);
long wcslen(int *s);
int *wcscpy(int *dst, int *src);
int wcscmp(int *a, int *b);
int putchar(int c);
typedef void FILE;
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
unsigned long fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int main(void) {
  char a[32];
  char b[32];
  char c[32];
  memset(a, 0, sizeof(a));
  memset(b, 0, sizeof(b));
  strcpy(a, "he");
  strcat(a, "llo");
  strncpy(b, a, 3);
  b[3] = 0;
  memcpy(c, a, 6);
  char *p = malloc(8);
  char *q = calloc(4, 1);
  int ws[8];
  int wd[8];
  ws[0] = 'A';
  ws[1] = 'b';
  ws[2] = 0;
  p[0] = 'O';
  p[1] = 'K';
  p[2] = 0;
  free(p);
  wcscpy(wd, ws);
  struct lconv *lc;
  setlocale(0, "C");
  lc = localeconv();
  FILE *wf = fopen("tmp.txt", "w");
  int wrote = fwrite("A\nB", 1, 3, wf);
  fclose(wf);
  char rb[8];
  FILE *rf = fopen("tmp.txt", "r");
  int readn = fread(rb, 1, 2, rf);
  int ch = fgetc(rf);
  fclose(rf);
  FILE *rf2 = fopen("tmp.txt", "r");
  char line[8];
  char *linep = fgets(line, sizeof(line), rf2);
  int ch2 = getc(rf2);
  fclose(rf2);
  return strlen(a) == 5 &&
         strcmp(a, "hello") == 0 &&
         strncmp(b, "helx", 3) == 0 &&
         memcmp(c, "hello", 6) == 0 &&
         strchr(a, 'l') == a + 2 &&
         strrchr(a, 'l') == a + 3 &&
         abs(-42) == 42 &&
         imaxabs(-1234567890123L) == 1234567890123L &&
         isdigit('7') && !isdigit('x') &&
         isalpha('Q') && !isalpha('7') &&
         toupper('q') == 'Q' &&
         iswdigit('8') && iswalpha('Z') && towupper('m') == 'M' &&
         wcslen(ws) == 2 && wcscmp(ws, wd) == 0 &&
         feclearexcept(31) == 0 && fetestexcept(16) == 16 &&
         lc->decimal_point[0] == '.' &&
         (int)(sqrt(2.0) * 1000.0) == 1414 &&
         (int)(sqrtf(2.0f) * 1000.0f) == 1414 &&
         (int)pow(2.0, 10.0) == 1024 &&
         (int)fabs(-3.5) == 3 &&
         atoi(" -123x") == -123 &&
         p != q && p[0] == 'O' && p[1] == 'K' && q[0] == 0 && q[3] == 0 &&
         wrote == 3 && readn == 2 && rb[0] == 'A' && rb[1] == '\n' && ch == 'B' &&
         linep == line && line[0] == 'A' && line[1] == '\n' && line[2] == 0 &&
         ch2 == 'B' &&
         putchar('Z') == 'Z' ? 42 : 1;
}
SRC

{
  for i in $(seq 0 16999); do
    printf 'int g%d = %d;\n' "$i" "$i"
  done
  printf 'int main(void) { return g16999 - 16957; }\n'
} > "$out_dir/many_globals.c"

"$root/build/ag_c_wasm" -c -o "$out_dir/main.o" "$out_dir/main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/other.o" "$out_dir/other.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked.wasm" \
  "$out_dir/main.o" "$out_dir/other.o"
wasm-validate "$out_dir/linked.wasm"
wasm-interp "$out_dir/linked.wasm" --run-all-exports > "$out_dir/linked.interp"
grep -q 'main() => i32:42' "$out_dir/linked.interp"

cp "$out_dir/main.o" "$out_dir/bad_reloc_target.o"
perl -0777 -pi -e 'my $name = "\x0areloc.CODE"; my $i = index($_, $name); die "missing reloc.CODE\n" if $i < 0; substr($_, $i + length($name), 1) = "\x01";' \
  "$out_dir/bad_reloc_target.o"
if "$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_bad_reloc_target.wasm" \
  "$out_dir/bad_reloc_target.o" "$out_dir/other.o" \
  > "$out_dir/linked_bad_reloc_target.out" 2> "$out_dir/linked_bad_reloc_target.err"; then
  echo "bad relocation target unexpectedly linked"
  exit 1
fi
grep -q 'reloc.CODE targets wrong section' "$out_dir/linked_bad_reloc_target.err"

"$root/build/ag_c_wasm" -c -o "$out_dir/gmain.o" "$out_dir/gmain.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/gother.o" "$out_dir/gother.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_global.wasm" \
  "$out_dir/gmain.o" "$out_dir/gother.o"
wasm-validate "$out_dir/linked_global.wasm"
wasm-interp "$out_dir/linked_global.wasm" --run-all-exports > "$out_dir/linked_global.interp"
grep -q 'main() => i32:42' "$out_dir/linked_global.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/addr_main.o" "$out_dir/addr_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/addr_other.o" "$out_dir/addr_other.c"
cp "$out_dir/addr_other.o" "$out_dir/bad_data_reloc_target.o"
perl -0777 -pi -e 'my $name = "\x0areloc.DATA"; my $i = index($_, $name); die "missing reloc.DATA\n" if $i < 0; substr($_, $i + length($name), 1) = "\x01";' \
  "$out_dir/bad_data_reloc_target.o"
if "$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_bad_data_reloc_target.wasm" \
  "$out_dir/addr_main.o" "$out_dir/bad_data_reloc_target.o" \
  > "$out_dir/linked_bad_data_reloc_target.out" 2> "$out_dir/linked_bad_data_reloc_target.err"; then
  echo "bad data relocation target unexpectedly linked"
  exit 1
fi
grep -q 'reloc.DATA targets wrong section' "$out_dir/linked_bad_data_reloc_target.err"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_addr.wasm" \
  "$out_dir/addr_main.o" "$out_dir/addr_other.o"
wasm-validate "$out_dir/linked_addr.wasm"
wasm-interp "$out_dir/linked_addr.wasm" --run-all-exports > "$out_dir/linked_addr.interp"
grep -q 'main() => i32:42' "$out_dir/linked_addr.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/static_main.o" "$out_dir/static_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/static_a.o" "$out_dir/static_a.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/static_b.o" "$out_dir/static_b.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_static.wasm" \
  "$out_dir/static_main.o" "$out_dir/static_a.o" "$out_dir/static_b.o"
wasm-validate "$out_dir/linked_static.wasm"
wasm-interp "$out_dir/linked_static.wasm" --run-all-exports > "$out_dir/linked_static.interp"
grep -q 'main() => i32:42' "$out_dir/linked_static.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/import_main.o" "$out_dir/import_main.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_import.wasm" \
  "$out_dir/import_main.o"
wasm-validate "$out_dir/linked_import.wasm"

"$root/build/ag_c_wasm" -c -o "$out_dir/fp_cross_main.o" "$out_dir/fp_cross_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/fp_cross_other.o" "$out_dir/fp_cross_other.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fp_cross.wasm" \
  "$out_dir/fp_cross_main.o" "$out_dir/fp_cross_other.o"
wasm-validate "$out_dir/linked_fp_cross.wasm"
wasm-interp "$out_dir/linked_fp_cross.wasm" --run-all-exports > "$out_dir/linked_fp_cross.interp"
grep -q 'main() => i32:42' "$out_dir/linked_fp_cross.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/fp_data_single.o" "$out_dir/fp_data_single.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fp_data_single.wasm" \
  "$out_dir/fp_data_single.o"
wasm-validate "$out_dir/linked_fp_data_single.wasm"
wasm-interp "$out_dir/linked_fp_data_single.wasm" --run-all-exports > "$out_dir/linked_fp_data_single.interp"
grep -q 'main() => i32:42' "$out_dir/linked_fp_data_single.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/fp_import.o" "$out_dir/fp_import.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fp_import.wasm" \
  "$out_dir/fp_import.o"
wasm-validate "$out_dir/linked_fp_import.wasm"

"$root/build/ag_c_wasm" -c -o "$out_dir/indirect_no_elem.o" "$out_dir/indirect_no_elem.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_indirect_no_elem.wasm" \
  "$out_dir/indirect_no_elem.o"
wasm-validate "$out_dir/linked_indirect_no_elem.wasm"
wasm-interp "$out_dir/linked_indirect_no_elem.wasm" --run-all-exports > "$out_dir/linked_indirect_no_elem.interp"
grep -q 'main() => i32:0' "$out_dir/linked_indirect_no_elem.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/fp_data_import.o" "$out_dir/fp_data_import.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fp_data_import.wasm" \
  "$out_dir/fp_data_import.o"
wasm-validate "$out_dir/linked_fp_data_import.wasm"

"$root/build/ag_c_wasm" -c -o "$out_dir/fp_data_cross_main.o" "$out_dir/fp_data_cross_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/fp_data_cross_other.o" "$out_dir/fp_data_cross_other.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fp_data_cross.wasm" \
  "$out_dir/fp_data_cross_main.o" "$out_dir/fp_data_cross_other.o"
wasm-validate "$out_dir/linked_fp_data_cross.wasm"
wasm-interp "$out_dir/linked_fp_data_cross.wasm" --run-all-exports > "$out_dir/linked_fp_data_cross.interp"
grep -q 'main() => i32:42' "$out_dir/linked_fp_data_cross.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/bss_big.o" "$out_dir/bss_big.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_bss_big.wasm" \
  "$out_dir/bss_big.o"
wasm-validate "$out_dir/linked_bss_big.wasm"
wasm-interp "$out_dir/linked_bss_big.wasm" --run-all-exports > "$out_dir/linked_bss_big.interp"
grep -q 'main() => i32:42' "$out_dir/linked_bss_big.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/bss_overflow.o" "$out_dir/bss_overflow.c"
if "$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_bss_overflow.wasm" \
  "$out_dir/bss_overflow.o" \
  > "$out_dir/linked_bss_overflow.out" 2> "$out_dir/linked_bss_overflow.err"; then
  echo "overflowing BSS layout unexpectedly linked"
  exit 1
fi
grep -q 'memory layout overflow' "$out_dir/linked_bss_overflow.err"

"$root/build/ag_c_wasm" -c -o "$out_dir/data_symbol_offset_main.o" "$out_dir/data_symbol_offset_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/data_symbol_offset_other.o" "$out_dir/data_symbol_offset_other.c"
cp "$out_dir/data_symbol_offset_other.o" "$out_dir/data_symbol_offset_other_patched.o"
perl -0777 -pi -e "s/\x01\x00\x05alias\x00\x00\x08/\x01\x00\x05alias\x00\x04\x04/" \
  "$out_dir/data_symbol_offset_other_patched.o"
wasm-objdump -x "$out_dir/data_symbol_offset_other_patched.o" > "$out_dir/data_symbol_offset_other.objdump"
grep -q 'D <alias> segment=0 offset=4 size=4' "$out_dir/data_symbol_offset_other.objdump"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_data_symbol_offset.wasm" \
  "$out_dir/data_symbol_offset_main.o" "$out_dir/data_symbol_offset_other_patched.o"
wasm-validate "$out_dir/linked_data_symbol_offset.wasm"
wasm-interp "$out_dir/linked_data_symbol_offset.wasm" --run-all-exports \
  > "$out_dir/linked_data_symbol_offset.interp"
grep -q 'main() => i32:42' "$out_dir/linked_data_symbol_offset.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/dup_func_main.o" "$out_dir/dup_func_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/dup_func_a.o" "$out_dir/dup_func_a.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/dup_func_b.o" "$out_dir/dup_func_b.c"
if "$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_dup_func.wasm" \
  "$out_dir/dup_func_main.o" "$out_dir/dup_func_a.o" "$out_dir/dup_func_b.o" \
  > "$out_dir/linked_dup_func.out" 2> "$out_dir/linked_dup_func.err"; then
  echo "duplicate function definition unexpectedly linked"
  exit 1
fi
grep -q 'duplicate symbol definition: dup' "$out_dir/linked_dup_func.err"

"$root/build/ag_c_wasm" -c -o "$out_dir/dup_data_main.o" "$out_dir/dup_data_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/dup_data_a.o" "$out_dir/dup_data_a.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/dup_data_b.o" "$out_dir/dup_data_b.c"
if "$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_dup_data.wasm" \
  "$out_dir/dup_data_main.o" "$out_dir/dup_data_a.o" "$out_dir/dup_data_b.o" \
  > "$out_dir/linked_dup_data.out" 2> "$out_dir/linked_dup_data.err"; then
  echo "duplicate data definition unexpectedly linked"
  exit 1
fi
grep -q 'duplicate symbol definition: dup_data' "$out_dir/linked_dup_data.err"

"$root/build/ag_c_wasm" -c -o "$out_dir/sig_mismatch_main.o" "$out_dir/sig_mismatch_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/sig_mismatch_other.o" "$out_dir/sig_mismatch_other.c"
if "$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_sig_mismatch.wasm" \
  "$out_dir/sig_mismatch_main.o" "$out_dir/sig_mismatch_other.o" \
  > "$out_dir/linked_sig_mismatch.out" 2> "$out_dir/linked_sig_mismatch.err"; then
  echo "signature mismatch unexpectedly linked"
  exit 1
fi
grep -q 'function signature mismatch: sig_mismatch' "$out_dir/linked_sig_mismatch.err"

"$root/build/ag_c_wasm" -c -o "$out_dir/import_sig_a.o" "$out_dir/import_sig_a.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/import_sig_b.o" "$out_dir/import_sig_b.c"
if "$root/build/ag_wasm_link" --no-entry -o "$out_dir/linked_import_sig.wasm" \
  "$out_dir/import_sig_a.o" "$out_dir/import_sig_b.o" \
  > "$out_dir/linked_import_sig.out" 2> "$out_dir/linked_import_sig.err"; then
  echo "import signature mismatch unexpectedly linked"
  exit 1
fi
grep -q 'function signature mismatch: host_mix' "$out_dir/linked_import_sig.err"

"$root/build/ag_c_wasm" -c -o "$out_dir/snprintf_negative.o" "$out_dir/snprintf_negative.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_snprintf_negative.wasm" \
  "$out_dir/snprintf_negative.o"
wasm-validate "$out_dir/linked_snprintf_negative.wasm"
wasm-interp "$out_dir/linked_snprintf_negative.wasm" --run-all-exports > "$out_dir/linked_snprintf_negative.interp"
grep -q 'main() => i32:42' "$out_dir/linked_snprintf_negative.interp"
if command -v wasm-objdump >/dev/null 2>&1; then
  "$root/build/ag_wasm_link" --nostdlib --no-entry --export=main -o "$out_dir/linked_snprintf_nostdlib.wasm" \
    "$out_dir/snprintf_negative.o"
  wasm-objdump -x "$out_dir/linked_snprintf_nostdlib.wasm" > "$out_dir/linked_snprintf_nostdlib.objdump"
  grep -q '<env.snprintf>' "$out_dir/linked_snprintf_nostdlib.objdump"
  grep -q '<env.sprintf>' "$out_dir/linked_snprintf_nostdlib.objdump"
fi

"$root/build/ag_c_wasm" -c -o "$out_dir/libc_runtime.o" "$out_dir/libc_runtime.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_libc_runtime.wasm" \
  "$out_dir/libc_runtime.o"
wasm-validate "$out_dir/linked_libc_runtime.wasm"
wasm-interp "$out_dir/linked_libc_runtime.wasm" --run-all-exports > "$out_dir/linked_libc_runtime.interp"
grep -q 'main() => i32:42' "$out_dir/linked_libc_runtime.interp"
if command -v wasm-objdump >/dev/null 2>&1; then
  "$root/build/ag_wasm_link" --nostdlib --no-entry --export=main -o "$out_dir/linked_libc_runtime_nostdlib.wasm" \
    "$out_dir/libc_runtime.o"
  wasm-objdump -x "$out_dir/linked_libc_runtime_nostdlib.wasm" > "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strlen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.memcpy>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.malloc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fopen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fread>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
fi

"$root/build/ag_c_wasm" -c -o "$out_dir/many_globals.o" "$out_dir/many_globals.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_many_globals.wasm" \
  "$out_dir/many_globals.o"
wasm-validate "$out_dir/linked_many_globals.wasm"
wasm-interp "$out_dir/linked_many_globals.wasm" --run-all-exports > "$out_dir/linked_many_globals.interp"
grep -q 'main() => i32:42' "$out_dir/linked_many_globals.interp"

echo "ag_wasm_link smoke: ok"
