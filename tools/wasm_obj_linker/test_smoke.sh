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

cat > "$out_dir/fp_return_sig.c" <<'SRC'
double square(double x) { return x * x; }
double apply(double (*f)(double), double v) { return f(v); }
int main(void) {
  double (*p)(double) = square;
  return (int)p(6.0) == 36 && (int)apply(square, 5.0) == 25 ? 42 : 1;
}
SRC

cat > "$out_dir/small_struct_return_sig.c" <<'SRC'
struct P { int x, y; };
struct P mk(int s) {
  struct P p = {0, 0};
  p.x = s;
  p.y = s * 2;
  return p;
}
struct P (*gop)(int) = mk;
int main(void) {
  struct P (*p)(int) = mk;
  struct P a = p(10);
  struct P b = gop(5);
  return a.x == 10 && a.y == 20 && b.x == 5 && b.y == 10 ? 42 : 1;
}
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
int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);
int iswdigit(int c);
int iswalnum(int c);
int iswalpha(int c);
int iswblank(int c);
int iswspace(int c);
int iswxdigit(int c);
int towlower(int c);
int towupper(int c);
void *malloc(long size);
void *calloc(long nmemb, long size);
void *realloc(void *ptr, long size);
void free(void *p);
int atexit(void *func);
double atof(char *s);
long atol(char *s);
long strtol(char *s, char **endptr, int base);
unsigned long strtoul(char *s, char **endptr, int base);
double strtod(char *s, char **endptr);
long strtoimax(char *s, char **endptr, int base);
unsigned long strtoumax(char *s, char **endptr, int base);
int rand(void);
void srand(int seed);
long labs(long n);
void qsort(void *base, long nmemb, long size, void *compar);
void *bsearch(void *key, void *base, long nmemb, long size, void *compar);
void exit(int status);
void abort(void);
char *getenv(char *name);
int system(char *command);
long time(long *tloc);
long clock(void);
double difftime(long end, long beginning);
int *__error(void);
typedef unsigned long long fexcept_t;
typedef struct { unsigned long long fpcr; unsigned long long fpsr; } fenv_t;
int feclearexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int feraiseexcept(int excepts);
int fesetexceptflag(fexcept_t *flagp, int excepts);
int fetestexcept(int excepts);
int fegetround(void);
int fesetround(int round);
int fegetenv(fenv_t *envp);
int feholdexcept(fenv_t *envp);
int fesetenv(fenv_t *envp);
int feupdateenv(fenv_t *envp);
char *setlocale(int category, char *locale);
struct lconv { char *decimal_point; };
struct lconv *localeconv(void);
double sqrt(double x);
float sqrtf(float x);
long double sqrtl(long double x);
double pow(double x, double y);
float powf(float x, float y);
long double powl(long double x, long double y);
float cbrtf(float x);
long double cbrtl(long double x);
double fabs(double x);
float fabsf(float x);
long double fabsl(long double x);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
float floorf(float x);
long double floorl(long double x);
float ceilf(float x);
long double ceill(long double x);
float roundf(float x);
long double roundl(long double x);
float truncf(float x);
long double truncl(long double x);
double fmod(double x, double y);
float fmodf(float x, float y);
long double fmodl(long double x, long double y);
double cbrt(double x);
double exp(double x);
float expf(float x);
long double expl(long double x);
double log(double x);
float logf(float x);
long double logl(long double x);
double log2(double x);
float log2f(float x);
long double log2l(long double x);
double log10(double x);
float log10f(float x);
long double log10l(long double x);
double atan(double x);
float atanf(float x);
long double atanl(long double x);
double atan2(double y, double x);
float atan2f(float y, float x);
long double atan2l(long double y, long double x);
double asin(double x);
float asinf(float x);
long double asinl(long double x);
double acos(double x);
float acosf(float x);
long double acosl(long double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double sin(double x);
float sinf(float x);
long double sinl(long double x);
double cos(double x);
float cosf(float x);
long double cosl(long double x);
double tan(double x);
float tanf(float x);
long double tanl(long double x);
double hypot(double x, double y);
float hypotf(float x, float y);
long double hypotl(long double x, long double y);
double fmin(double x, double y);
float fminf(float x, float y);
long double fminl(long double x, long double y);
double fmax(double x, double y);
float fmaxf(float x, float y);
long double fmaxl(long double x, long double y);
int atoi(char *s);
char *strcpy(char *dst, char *src);
char *strncpy(char *dst, char *src, unsigned long n);
char *strcat(char *dst, char *src);
char *strncat(char *dst, char *src, unsigned long n);
int strncmp(char *a, char *b, unsigned long n);
int memcmp(void *a, void *b, unsigned long n);
void *memmove(void *dst, void *src, unsigned long n);
void *memchr(void *s, int ch, unsigned long n);
char *strchr(char *s, int ch);
char *strrchr(char *s, int ch);
char *strstr(char *haystack, char *needle);
char *strtok(char *str, char *delim);
char *strerror(int errnum);
long wcslen(int *s);
int *wcscpy(int *dst, int *src);
int *wcsncpy(int *dst, int *src, unsigned long n);
int *wcscat(int *dst, int *src);
int *wcsncat(int *dst, int *src, unsigned long n);
int wcscmp(int *a, int *b);
int wcsncmp(int *a, int *b, unsigned long n);
int *wcschr(int *s, int ch);
int *wcsrchr(int *s, int ch);
int *wcsstr(int *s, int *sub);
int *wmemcpy(int *dst, int *src, unsigned long n);
int *wmemmove(int *dst, int *src, unsigned long n);
int *wmemset(int *s, int ch, unsigned long n);
int wmemcmp(int *a, int *b, unsigned long n);
int *wmemchr(int *s, int ch, unsigned long n);
long wcstol(int *s, int **endptr, int base);
unsigned long wcstoul(int *s, int **endptr, int base);
double wcstod(int *s, int **endptr);
unsigned long mbrtowc(int *pwc, char *s, unsigned long n, void *ps);
unsigned long wcrtomb(char *s, int wc, void *ps);
unsigned long mbrtoc16(unsigned short *pc16, char *s, unsigned long n, void *ps);
unsigned long c16rtomb(char *s, unsigned short c16, void *ps);
unsigned long mbrtoc32(unsigned int *pc32, char *s, unsigned long n, void *ps);
unsigned long c32rtomb(char *s, unsigned int c32, void *ps);
unsigned long mbsrtowcs(int *dst, char **src, unsigned long len, void *ps);
unsigned long wcsrtombs(char *dst, int **src, unsigned long len, void *ps);
int btowc(int c);
int wctob(int c);
int swprintf(int *s, unsigned long n, int *fmt, ...);
int swscanf(int *s, int *fmt, ...);
typedef void (*sig_handler_t)(int);
sig_handler_t signal(int sig, sig_handler_t handler);
int raise(int sig);
int wctype(char *property);
int iswctype(int wc, int desc);
int wctrans(char *property);
int towctrans(int wc, int desc);
int putchar(int c);
typedef void FILE;
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
unsigned long fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int printf(char *fmt, ...);
int fprintf(FILE *stream, char *fmt, ...);
int puts(char *s);
int fputs(char *s, FILE *stream);
int fputc(int c, FILE *stream);
int fflush(FILE *stream);
void perror(char *s);
int getchar(void);
int int_cmp(long ap, long bp) {
  int *a = (int *)ap;
  int *b = (int *)bp;
  return *a - *b;
}
int main(void) {
  char a[32];
  char b[32];
  char c[32];
  char d[32];
  char e[32];
  char toks[32];
  memset(a, 0, sizeof(a));
  memset(b, 0, sizeof(b));
  strcpy(a, "he");
  strcat(a, "llo");
  strcpy(d, "ab");
  strncat(d, "cdef", 2);
  strcpy(e, "abcdef");
  memmove(e + 2, e, 4);
  strcpy(toks, "aa,bb;cc");
  char *nullp = 0;
  strncpy(b, a, 3);
  b[3] = 0;
  memcpy(c, a, 6);
  char *tok1 = strtok(toks, ",;");
  char *tok2 = strtok(nullp, ",;");
  char *tok3 = strtok(nullp, ",;");
  char *tok4 = strtok(nullp, ",;");
  char *p = malloc(8);
  char *q = calloc(4, 1);
  char *r = malloc(2);
  r[0] = 'A';
  r[1] = 0;
  r = realloc(r, 4);
  char *endp = 0;
  char *uendp = 0;
  char *dendp = 0;
  char *imax_endp = 0;
  char *umax_endp = 0;
  long parsed = strtol("  -2a", &endp, 16);
  unsigned long parsed_u = strtoul("  ff!", &uendp, 16);
  double parsed_d = strtod(" -12.5e1!", &dendp);
  double parsed_atof = atof(" 3.25x");
  long parsed_imax = strtoimax("  7f!", &imax_endp, 16);
  unsigned long parsed_umax = strtoumax("  377!", &umax_endp, 8);
  srand(1);
  int rand1 = rand();
  int rand2 = rand();
  int nums[5];
  int key = 3;
  int *found;
  int never = 0;
  void *nullv = 0;
  long tloc = 123;
  int *errp = __error();
  sig_handler_t sigh = 0;
  fexcept_t flag = 0;
  fenv_t env = {0, 0};
  int ws[8];
  int wd[8];
  int we[8];
  int wfbuf[8];
  int convw[8];
  char convc[8];
  int swbuf[16];
  int swfmt[8];
  int swarg[4];
  int scanfmt[4];
  unsigned short c16 = 0;
  unsigned int c32 = 0;
  int *wend = 0;
  int wcnum[8];
  int wcdec[8];
  wcnum[0] = ' ';
  wcnum[1] = '-';
  wcnum[2] = '2';
  wcnum[3] = 'a';
  wcnum[4] = '.';
  wcnum[5] = '5';
  wcnum[6] = 0;
  wcdec[0] = '2';
  wcdec[1] = '.';
  wcdec[2] = '5';
  wcdec[3] = 0;
  ws[0] = 'A';
  ws[1] = 'b';
  ws[2] = 0;
  p[0] = 'O';
  p[1] = 'K';
  p[2] = 0;
  free(p);
  nums[0] = 4;
  nums[1] = 1;
  nums[2] = 5;
  nums[3] = 2;
  nums[4] = 3;
  qsort(nums, 5, sizeof(int), int_cmp);
  found = bsearch(&key, nums, 5, sizeof(int), int_cmp);
  if (never) {
    exit(7);
    abort();
  }
  wcscpy(wd, ws);
  wcsncpy(we, ws, 3);
  wcscat(we, ws);
  wcsncat(we, ws + 1, 1);
  wmemcpy(wfbuf, we, 4);
  wmemmove(wfbuf + 1, wfbuf, 3);
  wmemset(wfbuf + 4, 'Z', 2);
  char *mbsrc = "Hi";
  char *mbsrcp = mbsrc;
  int *wcsrcp;
  mbsrtowcs(convw, &mbsrcp, 8, nullv);
  wcsrcp = convw;
  wcsrtombs(convc, &wcsrcp, 8, nullv);
  unsigned long m16 = mbrtoc16(&c16, "U", 2, nullv);
  unsigned long r16 = c16rtomb(convc + 2, c16, nullv);
  unsigned long m32 = mbrtoc32(&c32, "V", 2, nullv);
  unsigned long r32 = c32rtomb(convc + 3, c32, nullv);
  swfmt[0] = '%';
  swfmt[1] = 'd';
  swfmt[2] = '-';
  swfmt[3] = '%';
  swfmt[4] = 'l';
  swfmt[5] = 's';
  swfmt[6] = 0;
  swarg[0] = 'O';
  swarg[1] = 'K';
  swarg[2] = 0;
  int swret = swprintf(swbuf, 16, swfmt, 12, swarg);
  scanfmt[0] = '%';
  scanfmt[1] = 'd';
  scanfmt[2] = 0;
  int scanret = swscanf(swbuf, scanfmt, &never);
  struct lconv *lc;
  setlocale(0, "C");
  lc = localeconv();
  FILE *wf = fopen("tmp.txt", "w");
  int wrote = fwrite("A\nB", 1, 3, wf);
  fclose(wf);
  char rb[8];
  FILE *rf = fopen("tmp.txt", "r");
  int readn = fread(rb, 1, 2, rf);
  long pos_after_read = ftell(rf);
  int ch = fgetc(rf);
  int eof_after_ch = feof(rf);
  int eof_read = fgetc(rf);
  int eof_after_miss = feof(rf);
  int seek_ok = fseek(rf, 1, 0);
  long pos_after_seek = ftell(rf);
  int ch_seek = fgetc(rf);
  int bad_seek = fseek(rf, -99, 0);
  int err_after_bad_seek = ferror(rf);
  clearerr(rf);
  int err_after_clear = ferror(rf);
  rewind(rf);
  long pos_after_rewind = ftell(rf);
  fclose(rf);
  FILE *rf2 = fopen("tmp.txt", "r");
  char line[8];
  char *linep = fgets(line, sizeof(line), rf2);
  int ch2 = getc(rf2);
  fclose(rf2);
  int sin0 = (int)(sin(0.0) * 1000.0);
  int sin90 = (int)(sin(1.5707963267948966) * 1000.0);
  int sinm90 = (int)(sin(-1.5707963267948966) * 1000.0);
  int cos0 = (int)(cos(0.0) * 1000.0);
  int cos90 = (int)(cos(1.5707963267948966) * 1000.0);
  int cos180 = (int)(cos(3.141592653589793) * 1000.0);
  int tan45 = (int)(tan(0.7853981633974483) * 1000.0);
  int sinf90 = (int)(sinf(1.5707963267948966f) * 1000.0f);
  int sinl90 = (int)(sinl(1.5707963267948966L) * 1000.0L);
  int cosf180 = (int)(cosf(3.141592653589793f) * 1000.0f);
  int cosl180 = (int)(cosl(3.141592653589793L) * 1000.0L);
  int tanf45 = (int)(tanf(0.7853981633974483f) * 1000.0f);
  int tanl45 = (int)(tanl(0.7853981633974483L) * 1000.0L);
  int fmod_pos = (int)(fmod(7.5, 2.0) * 1000.0);
  int fmod_neg = (int)(fmod(-7.5, 2.0) * 1000.0);
  int fmodf_pos = (int)(fmodf(7.5f, 2.0f) * 1000.0f);
  int fmodl_pos = (int)(fmodl(7.5L, 2.0L) * 1000.0L);
  int cbrt_pos = (int)(cbrt(27.0) * 1000.0);
  int cbrt_neg = (int)(cbrt(-8.0) * 1000.0);
  int cbrtf_pos = (int)(cbrtf(27.0f) * 1000.0f);
  int cbrtl_neg = (int)(cbrtl(-8.0L) * 1000.0L);
  int exp1 = (int)(exp(1.0) * 1000.0);
  int expf1 = (int)(expf(1.0f) * 1000.0f);
  int expl1 = (int)(expl(1.0L) * 1000.0L);
  int loge = (int)(log(2.718281828459045) * 1000.0);
  int logfe = (int)(logf(2.718281828459045f) * 1000.0f);
  int logle = (int)(logl(2.718281828459045L) * 1000.0L);
  int log2v = (int)(log2(8.0) * 1000.0);
  int log2fv = (int)(log2f(8.0f) * 1000.0f);
  int log2lv = (int)(log2l(8.0L) * 1000.0L);
  int log10v = (int)(log10(100.0) * 1000.0);
  int log10fv = (int)(log10f(100.0f) * 1000.0f);
  int log10lv = (int)(log10l(100.0L) * 1000.0L);
  int pow_int = (int)(pow(-2.0, 3.0) * 1000.0);
  int pow_frac = (int)(pow(9.0, 0.5) * 1000.0);
  int powf_int = (int)(powf(2.0f, 5.0f) * 1000.0f);
  int powl_int = (int)(powl(2.0L, 4.0L) * 1000.0L);
  int atan1 = (int)(atan(1.0) * 1000.0);
  int atanf1 = (int)(atanf(1.0f) * 1000.0f);
  int atanl1 = (int)(atanl(1.0L) * 1000.0L);
  int atan2v = (int)(atan2(1.0, 0.0) * 1000.0);
  int atan2fv = (int)(atan2f(1.0f, 0.0f) * 1000.0f);
  int atan2lv = (int)(atan2l(1.0L, 0.0L) * 1000.0L);
  int asinv = (int)(asin(1.0) * 1000.0);
  int asinfv = (int)(asinf(1.0f) * 1000.0f);
  int asinlv = (int)(asinl(1.0L) * 1000.0L);
  int acosv = (int)(acos(0.0) * 1000.0);
  int acosfv = (int)(acosf(0.0f) * 1000.0f);
  int acosl_v = (int)(acosl(0.0L) * 1000.0L);
  int sinh0 = (int)(sinh(0.0) * 1000.0);
  int cosh0 = (int)(cosh(0.0) * 1000.0);
  int tanh0 = (int)(tanh(0.0) * 1000.0);
  int tanh1 = (int)(tanh(1.0) * 1000.0);
  return strlen(a) == 5 &&
         strcmp(a, "hello") == 0 &&
         strcmp(d, "abcd") == 0 &&
         e[0] == 'a' && e[1] == 'b' && e[2] == 'a' && e[3] == 'b' &&
         e[4] == 'c' && e[5] == 'd' &&
         strncmp(b, "helx", 3) == 0 &&
         memcmp(c, "hello", 6) == 0 &&
         memchr(c, 'l', 6) == c + 2 &&
         strchr(a, 'l') == a + 2 &&
         strrchr(a, 'l') == a + 3 &&
         strstr(a, "ll") == a + 2 &&
         tok1 == toks && strcmp(tok1, "aa") == 0 &&
         strcmp(tok2, "bb") == 0 && strcmp(tok3, "cc") == 0 && tok4 == 0 &&
         strerror(5)[0] == 'e' &&
         abs(-42) == 42 &&
         labs(-1234567890123L) == 1234567890123L &&
         imaxabs(-1234567890123L) == 1234567890123L &&
         atol(" -1234x") == -1234 &&
         parsed == -42 && *endp == 0 &&
         parsed_u == 255 && *uendp == '!' &&
         (int)parsed_d == -125 && *dendp == '!' &&
         (int)(parsed_atof * 100.0) == 325 &&
         parsed_imax == 127 && *imax_endp == '!' &&
         parsed_umax == 255 && *umax_endp == '!' &&
         rand1 != rand2 &&
         atexit(nullv) == 0 && getenv("AGC_MISSING_ENV") == 0 && system("true") == 0 &&
         nums[0] == 1 && nums[1] == 2 && nums[2] == 3 && nums[3] == 4 && nums[4] == 5 &&
         found == nums + 2 && *found == 3 &&
         time(&tloc) == 0 && tloc == 0 && clock() == 0 &&
         (int)difftime(100, 58) == 42 &&
         errp != 0 && (*errp = 34, *__error() == 34) &&
         signal(2, sigh) == 0 && raise(2) == 0 &&
         isdigit('7') && !isdigit('x') &&
         isalnum('A') && isalnum('9') && !isalnum('!') &&
         isalpha('Q') && !isalpha('7') &&
         isblank('\t') && !isblank('\n') &&
         iscntrl('\n') && !iscntrl('A') &&
         isgraph('!') && !isgraph(' ') &&
         islower('q') && !islower('Q') &&
         isprint(' ') && !isprint('\n') &&
         ispunct('!') && !ispunct('A') &&
         isspace('\n') && !isspace('A') &&
         isupper('Q') && !isupper('q') &&
         isxdigit('f') && isxdigit('A') && !isxdigit('g') &&
         tolower('Q') == 'q' &&
         toupper('q') == 'Q' &&
         iswdigit('8') && iswalnum('Z') && iswalpha('Z') &&
         iswblank('\t') && iswspace('\n') && iswxdigit('F') &&
         towlower('M') == 'm' && towupper('m') == 'M' &&
         iswctype('7', wctype("digit")) && !iswctype('x', wctype("digit")) &&
         towctrans('q', wctrans("toupper")) == 'Q' &&
         wcslen(ws) == 2 && wcscmp(ws, wd) == 0 &&
         wcsncmp(we, ws, 2) == 0 && wcschr(we, 'b') == we + 1 &&
         wcsrchr(we, 'b') == we + 4 &&
         wcsstr(we, ws) == we &&
         wmemcmp(wd, ws, 3) == 0 && wmemchr(wfbuf, 'Z', 6) == wfbuf + 4 &&
         wcstol(wcnum, &wend, 16) == -42 && *wend == '.' &&
         wcstoul(wcnum + 2, &wend, 16) == 42 && *wend == '.' &&
         (int)(wcstod(wcdec, &wend) * 10.0) == 25 && *wend == 0 &&
         mbrtowc(convw, "Q", 2, nullv) == 1 && convw[0] == 'Q' &&
         wcrtomb(convc, 'R', nullv) == 1 && convc[0] == 'R' &&
         convw[0] == 'Q' && convc[0] == 'R' &&
         m16 == 1 && c16 == 'U' && r16 == 1 &&
         m32 == 1 && c32 == 'V' && r32 == 1 && convc[2] == 'U' && convc[3] == 'V' &&
         btowc('S') == 'S' && wctob('T') == 'T' &&
         swret == 5 && swbuf[0] == '1' && swbuf[1] == '2' && swbuf[2] == '-' &&
         swbuf[3] == 'O' && swbuf[4] == 'K' && swbuf[5] == 0 && scanret == 0 &&
         feclearexcept(31) == 0 && fegetexceptflag(&flag, 16) == 0 && flag == 16 &&
         feraiseexcept(4) == 0 && fesetexceptflag(&flag, 16) == 0 &&
         fetestexcept(16) == 16 &&
         fesetround(0x00400000) == 0 && fegetround() == 0x00400000 &&
         fegetenv(&env) == 0 && feholdexcept(&env) == 0 &&
         fesetenv(&env) == 0 && feupdateenv(&env) == 0 &&
         lc->decimal_point[0] == '.' &&
         (int)(sqrt(2.0) * 1000.0) == 1414 &&
         (int)(sqrtf(2.0f) * 1000.0f) == 1414 &&
         (int)(sqrtl(2.0L) * 1000.0L) == 1414 &&
         (int)pow(2.0, 10.0) == 1024 &&
         pow_int == -8000 &&
         pow_frac >= 2998 && pow_frac <= 3002 &&
         powf_int == 32000 &&
         powl_int == 16000 &&
         (int)fabs(-3.5) == 3 &&
         (int)fabsf(-2.5f) == 2 &&
         (int)fabsl(-4.5L) == 4 &&
         (int)floor(3.8) == 3 && (int)floor(-3.2) == -4 &&
         (int)ceil(3.2) == 4 && (int)ceil(-3.8) == -3 &&
         (int)round(3.5) == 4 && (int)round(-3.5) == -4 &&
         (int)trunc(3.8) == 3 && (int)trunc(-3.8) == -3 &&
         (int)floorf(2.9f) == 2 && (int)ceilf(2.1f) == 3 &&
         (int)roundf(-2.5f) == -3 &&
         (int)floorl(2.9L) == 2 && (int)ceill(2.1L) == 3 &&
         (int)roundl(-2.5L) == -3 &&
         (int)truncf(-2.8f) == -2 && (int)truncl(-2.8L) == -2 &&
         sin0 == 0 && sin90 >= 998 && sin90 <= 1002 &&
         sinm90 <= -998 && sinm90 >= -1002 &&
         cos0 >= 998 && cos0 <= 1002 &&
         cos90 >= -2 && cos90 <= 2 &&
         cos180 <= -998 && cos180 >= -1002 &&
         tan45 >= 998 && tan45 <= 1002 &&
         sinf90 >= 998 && sinf90 <= 1002 &&
         sinl90 >= 998 && sinl90 <= 1002 &&
         cosf180 <= -998 && cosf180 >= -1002 &&
         cosl180 <= -998 && cosl180 >= -1002 &&
         tanf45 >= 998 && tanf45 <= 1002 &&
         tanl45 >= 998 && tanl45 <= 1002 &&
         fmod_pos == 1500 && fmod_neg == -1500 &&
         fmodf_pos == 1500 &&
         fmodl_pos == 1500 &&
         cbrt_pos >= 2998 && cbrt_pos <= 3002 &&
         cbrt_neg >= -2002 && cbrt_neg <= -1998 &&
         cbrtf_pos >= 2998 && cbrtf_pos <= 3002 &&
         cbrtl_neg >= -2002 && cbrtl_neg <= -1998 &&
         exp1 >= 2716 && exp1 <= 2720 &&
         expf1 >= 2716 && expf1 <= 2720 &&
         expl1 >= 2716 && expl1 <= 2720 &&
         loge >= 998 && loge <= 1002 &&
         logfe >= 998 && logfe <= 1002 &&
         logle >= 998 && logle <= 1002 &&
         log2v >= 2998 && log2v <= 3002 &&
         log2fv >= 2998 && log2fv <= 3002 &&
         log2lv >= 2998 && log2lv <= 3002 &&
         log10v >= 1998 && log10v <= 2002 &&
         log10fv >= 1998 && log10fv <= 2002 &&
         log10lv >= 1998 && log10lv <= 2002 &&
         atan1 >= 783 && atan1 <= 787 &&
         atanf1 >= 783 && atanf1 <= 787 &&
         atanl1 >= 783 && atanl1 <= 787 &&
         atan2v >= 1568 && atan2v <= 1572 &&
         atan2fv >= 1568 && atan2fv <= 1572 &&
         atan2lv >= 1568 && atan2lv <= 1572 &&
         asinv >= 1568 && asinv <= 1572 &&
         asinfv >= 1568 && asinfv <= 1572 &&
         asinlv >= 1568 && asinlv <= 1572 &&
         acosv >= 1568 && acosv <= 1572 &&
         acosfv >= 1568 && acosfv <= 1572 &&
         acosl_v >= 1568 && acosl_v <= 1572 &&
         (int)(hypot(3.0, 4.0) * 1000.0) == 5000 &&
         (int)(hypotf(3.0f, 4.0f) * 1000.0f) == 5000 &&
         (int)(hypotl(3.0L, 4.0L) * 1000.0L) == 5000 &&
         (int)(fmin(3.0, 4.0) * 1000.0) == 3000 &&
         (int)(fminf(3.0f, 4.0f) * 1000.0f) == 3000 &&
         (int)(fminl(3.0L, 4.0L) * 1000.0L) == 3000 &&
         (int)(fmax(3.0, 4.0) * 1000.0) == 4000 &&
         (int)(fmaxf(3.0f, 4.0f) * 1000.0f) == 4000 &&
         (int)(fmaxl(3.0L, 4.0L) * 1000.0L) == 4000 &&
         sinh0 == 0 && cosh0 >= 998 && cosh0 <= 1002 &&
         tanh0 == 0 && tanh1 >= 759 && tanh1 <= 763 &&
         atoi(" -123x") == -123 &&
         p != q && p[0] == 'O' && p[1] == 'K' && q[0] == 0 && q[3] == 0 &&
         r[0] == 'A' &&
         wrote == 3 && readn == 2 && rb[0] == 'A' && rb[1] == '\n' && ch == 'B' &&
         pos_after_read == 2 && !eof_after_ch && eof_read == -1 && eof_after_miss &&
         seek_ok == 0 && pos_after_seek == 1 && ch_seek == '\n' &&
         bad_seek == -1 && err_after_bad_seek && !err_after_clear && pos_after_rewind == 0 &&
         linep == line && line[0] == 'A' && line[1] == '\n' && line[2] == 0 &&
         ch2 == 'B' &&
         printf("value=%d/%u/%s/%c/%%", -12, 345u, "ok", 'Z') == 20 &&
         fprintf(0, "[%04d]", 7) == 6 &&
         puts("ok") == 3 &&
         fputs("abc", 0) == 3 &&
         fputc('R', 0) == 'R' &&
         fflush(0) == 0 &&
         (perror("ignored"), 1) &&
         getchar() == -1 &&
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

"$root/build/ag_c_wasm" -c -o "$out_dir/fp_return_sig.o" "$out_dir/fp_return_sig.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fp_return_sig.wasm" \
  "$out_dir/fp_return_sig.o"
wasm-validate "$out_dir/linked_fp_return_sig.wasm"
wasm-interp "$out_dir/linked_fp_return_sig.wasm" --run-all-exports > "$out_dir/linked_fp_return_sig.interp"
grep -q 'main() => i32:42' "$out_dir/linked_fp_return_sig.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/small_struct_return_sig.o" "$out_dir/small_struct_return_sig.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_small_struct_return_sig.wasm" \
  "$out_dir/small_struct_return_sig.o"
wasm-validate "$out_dir/linked_small_struct_return_sig.wasm"
wasm-interp "$out_dir/linked_small_struct_return_sig.wasm" --run-all-exports > "$out_dir/linked_small_struct_return_sig.interp"
grep -q 'main() => i32:42' "$out_dir/linked_small_struct_return_sig.interp"

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
  grep -q '<env.memmove>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.memchr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strncat>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strstr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtok>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strerror>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.malloc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.realloc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atof>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atol>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtol>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoul>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtod>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoimax>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoumax>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.qsort>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.bsearch>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.rand>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.srand>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.labs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atexit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.exit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.abort>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getenv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.system>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.time>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.clock>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.difftime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.__error>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.signal>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.raise>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fegetexceptflag>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.feraiseexcept>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fesetexceptflag>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fegetround>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fesetround>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fegetenv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.feholdexcept>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fesetenv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.feupdateenv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isalnum>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isspace>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tolower>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.iswalnum>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.towlower>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wctype>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.iswctype>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wctrans>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.towctrans>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsncpy>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcscat>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsncat>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsncmp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcschr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsrchr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsstr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemcpy>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemmove>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemset>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemcmp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemchr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstol>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstoul>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstod>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbrtowc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcrtomb>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbrtoc16>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.c16rtomb>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbrtoc32>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.c32rtomb>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbsrtowcs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsrtombs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.btowc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wctob>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.swprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.swscanf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fopen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fread>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fseek>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ftell>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.rewind>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.feof>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ferror>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.clearerr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sin>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sinf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sinl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cos>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cosf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cosl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tan>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tanf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tanl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.floor>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.floorl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ceilf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ceill>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.roundl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.truncf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.truncl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmod>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmodf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmodl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.powf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.powl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sqrtl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cbrtf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cbrtl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fabsl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.exp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.expf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.expl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.logf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.logl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log2f>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log2l>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log10>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log10f>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log10l>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atanf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atanl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atan2>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atan2f>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atan2l>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.asinf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.asinl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.acos>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.acosf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.acosl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.hypot>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.hypotf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.hypotl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmin>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fminf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fminl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmax>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmaxf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmaxl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sinh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tanh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.printf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.puts>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fputs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fputc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fflush>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.perror>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getchar>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
fi

cat > "$out_dir/stdio_data_runtime.c" <<'SRC'
typedef void FILE;
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
int main(void) {
  return __stdinp == 0 && __stdoutp == 0 && __stderrp == 0 ? 42 : 1;
}
SRC
"$root/build/ag_c_wasm" -c -o "$out_dir/stdio_data_runtime.o" "$out_dir/stdio_data_runtime.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_stdio_data_runtime.wasm" \
  "$out_dir/stdio_data_runtime.o"
wasm-validate "$out_dir/linked_stdio_data_runtime.wasm"
wasm-interp "$out_dir/linked_stdio_data_runtime.wasm" --run-all-exports > "$out_dir/linked_stdio_data_runtime.interp"
grep -q 'main() => i32:42' "$out_dir/linked_stdio_data_runtime.interp"

cat > "$out_dir/assert_runtime.c" <<'SRC'
void __assert_rtn(char *func, char *file, int line, char *expr);
int ag_assert_gate;
int main(void) {
  if (ag_assert_gate) __assert_rtn("main", "assert_runtime.c", 4, "ag_assert_gate == 0");
  return 42;
}
SRC
"$root/build/ag_c_wasm" -c -o "$out_dir/assert_runtime.o" "$out_dir/assert_runtime.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_assert_runtime.wasm" \
  "$out_dir/assert_runtime.o"
wasm-validate "$out_dir/linked_assert_runtime.wasm"
wasm-interp "$out_dir/linked_assert_runtime.wasm" --run-all-exports > "$out_dir/linked_assert_runtime.interp"
grep -q 'main() => i32:42' "$out_dir/linked_assert_runtime.interp"
if command -v wasm-objdump >/dev/null 2>&1; then
  "$root/build/ag_wasm_link" --nostdlib --no-entry --export=main -o "$out_dir/linked_assert_runtime_nostdlib.wasm" \
    "$out_dir/assert_runtime.o"
  wasm-objdump -x "$out_dir/linked_assert_runtime_nostdlib.wasm" > "$out_dir/linked_assert_runtime_nostdlib.objdump"
  grep -q '<env.__assert_rtn>' "$out_dir/linked_assert_runtime_nostdlib.objdump"
fi

"$root/build/ag_c_wasm" -c -o "$out_dir/many_globals.o" "$out_dir/many_globals.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_many_globals.wasm" \
  "$out_dir/many_globals.o"
wasm-validate "$out_dir/linked_many_globals.wasm"
wasm-interp "$out_dir/linked_many_globals.wasm" --run-all-exports > "$out_dir/linked_many_globals.interp"
grep -q 'main() => i32:42' "$out_dir/linked_many_globals.interp"

echo "ag_wasm_link smoke: ok"
