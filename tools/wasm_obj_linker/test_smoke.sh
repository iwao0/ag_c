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

cat > "$out_dir/multi_export.c" <<'SRC'
int answer(void) { return 7; }
int main(void) { return answer() * 6; }
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

cat > "$out_dir/indirect_aggregate_return_sig.c" <<'SRC'
struct Big { long a, b, c; };
struct Big mk(long x) {
  struct Big v = {x, x + 1, x + 2};
  return v;
}
long apply(struct Big (*fp)(long), long x) {
  struct Big v = fp(x);
  return v.a + v.b + v.c;
}
int main(void) { return apply(mk, 13) == 42 ? 42 : 1; }
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

cat > "$out_dir/snprintf_float.c" <<'SRC'
int snprintf(char *s, unsigned long n, const char *fmt, ...);
int main(void) {
  char a[32];
  char b[32];
  char c[32];
  int na = snprintf(a, sizeof(a), "%.1f", 3.14);
  int nb = snprintf(b, sizeof(b), "%.2f", -2.25);
  int nc = snprintf(c, sizeof(c), "%.1Lf", (long double)4.04);
  return na == 3 && a[0] == '3' && a[1] == '.' && a[2] == '1' && a[3] == 0 &&
         nb == 5 && b[0] == '-' && b[1] == '2' && b[2] == '.' && b[3] == '2' &&
         b[4] == '5' && b[5] == 0 &&
         nc == 3 && c[0] == '4' && c[1] == '.' && c[2] == '0' && c[3] == 0 ? 42 : 1;
}
SRC

cat > "$out_dir/vformat_file.c" <<'SRC'
typedef long va_list;
#define va_start(ap, last) ((void)(last), (ap) = (va_list)__va_arg_area)
#define va_end(ap) ((void)(ap))
typedef void FILE;
int vsnprintf(char *buf, unsigned long size, char *fmt, va_list ap);
int vfprintf(FILE *stream, char *fmt, va_list ap);
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int fgetc(FILE *stream);
long ftell(FILE *stream);
int call_vsnprintf(char *buf, unsigned long size, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}
int call_vfprintf(FILE *stream, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vfprintf(stream, fmt, ap);
  va_end(ap);
  return n;
}
int main(void) {
  char buf[8];
  int sn = call_vsnprintf(buf, sizeof(buf), "%d-%s", 31, "ok");
  FILE *wf = fopen("tmp.txt", "w");
  int fn = call_vfprintf(wf, "V%d", 5);
  long pos = ftell(wf);
  fclose(wf);
  FILE *rf = fopen("tmp.txt", "r");
  char rb[3];
  unsigned long got = fread(rb, 1, 2, rf);
  int eof = fgetc(rf);
  fclose(rf);
  return sn == 5 && buf[0] == '3' && buf[1] == '1' && buf[2] == '-' &&
         buf[3] == 'o' && buf[4] == 'k' && buf[5] == 0 &&
         fn == 2 && pos == 2 && got == 2 &&
         rb[0] == 'V' && rb[1] == '5' && eof == -1 ? 42 : 1;
}
SRC

cat > "$out_dir/wide_locale_state.c" <<'SRC'
struct lconv {
  char *decimal_point;
};
char *setlocale(int category, char *locale);
struct lconv *localeconv(void);
unsigned long mbsrtowcs(int *dst, char **src, unsigned long len, void *ps);
unsigned long wcsrtombs(char *dst, int **src, unsigned long len, void *ps);
int main(void) {
  void *nullv = 0;
  char *mbsrc = "Hi";
  char *mbsrcp = mbsrc;
  int wide[8];
  unsigned long wn = mbsrtowcs(wide, &mbsrcp, 8, nullv);
  int *widep = wide;
  char narrow[8];
  unsigned long nn = wcsrtombs(narrow, &widep, 8, nullv);
  char *loc = setlocale(0, "C");
  struct lconv *lc = localeconv();
  return wn == 2 && mbsrcp == 0 && wide[0] == 'H' && wide[1] == 'i' &&
         wide[2] == 0 && nn == 2 && widep == 0 && narrow[0] == 'H' &&
         narrow[1] == 'i' && narrow[2] == 0 &&
         loc != 0 && loc[0] == 'C' && loc[1] == 0 &&
         lc != 0 && lc->decimal_point[0] == '.' && lc->decimal_point[1] == 0 ? 42 : 1;
}
SRC

cat > "$out_dir/fenv_state.c" <<'SRC'
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
int main(void) {
  fexcept_t flag = 0;
  fenv_t env = {0, 0};
  int ok = 1;
  ok = ok && feclearexcept(31) == 0 && fetestexcept(31) == 0;
  ok = ok && feraiseexcept(4) == 0 && fetestexcept(31) == 4;
  ok = ok && fegetexceptflag(&flag, 31) == 0 && flag == 4;
  ok = ok && feclearexcept(4) == 0 && fetestexcept(31) == 0;
  ok = ok && fesetexceptflag(&flag, 16) == 0 && fetestexcept(31) == 0;
  ok = ok && fesetexceptflag(&flag, 4) == 0 && fetestexcept(31) == 4;
  ok = ok && fesetround(0x00400000) == 0 && fegetround() == 0x00400000;
  ok = ok && fegetenv(&env) == 0 && env.fpcr == 0x00400000 && env.fpsr == 4;
  ok = ok && feraiseexcept(16) == 0 && feholdexcept(&env) == 0 &&
            env.fpsr == 20 && fetestexcept(31) == 0;
  ok = ok && fesetenv(&env) == 0 && fegetround() == 0x00400000 && fetestexcept(31) == 20;
  ok = ok && feclearexcept(31) == 0 && feraiseexcept(1) == 0 &&
            feupdateenv(&env) == 0 && fetestexcept(31) == 21;
  return ok ? 42 : 1;
}
SRC

cat > "$out_dir/signal_state.c" <<'SRC'
typedef void (*sig_handler_t)(int);
sig_handler_t signal(int sig, sig_handler_t handler);
int raise(int sig);
int seen;
void handler(int sig) {
  seen = sig + 30;
}
int main(void) {
  sig_handler_t old = signal(2, handler);
  int rc = raise(2);
  sig_handler_t old2 = signal(2, 0);
  int rc2 = raise(2);
  int bad = raise(99);
  return old == 0 && rc == 0 && seen == 32 &&
         old2 == handler && rc2 == 0 && seen == 32 && bad == -1 ? 42 : 1;
}
SRC

cat > "$out_dir/strto_base.c" <<'SRC'
long strtol(char *s, char **endptr, int base);
unsigned long strtoul(char *s, char **endptr, int base);
unsigned long strtoumax(char *s, char **endptr, int base);
int main(void) {
  char *end = 0;
  char *none = "xyz";
  long hex = strtol("0x10!", &end, 0);
  int ok_hex = hex == 16 && *end == '!';
  long oct = strtol("010!", &end, 0);
  int ok_oct = oct == 8 && *end == '!';
  unsigned long ux = strtoul("+0X1f!", &end, 0);
  int ok_ux = ux == 31 && *end == '!';
  unsigned long uneg = strtoumax("-010!", &end, 0);
  int ok_uneg = uneg == (0UL - 8UL) && *end == '!';
  long no = strtol(none, &end, 0);
  int ok_none = no == 0 && end == none;
  return ok_hex && ok_oct && ok_ux && ok_uneg && ok_none ? 42 : 1;
}
SRC

cat > "$out_dir/strtod_state.c" <<'SRC'
double strtod(char *s, char **endptr);
int main(void) {
  char *end = 0;
  char *none = "  +xyz";
  char *dot = ".x";
  double hex = strtod("0x1.8p+3!", &end);
  int ok_hex = hex == 12.0 && *end == '!';
  double lead_dot = strtod(".25!", &end);
  int ok_dot = lead_dot == 0.25 && *end == '!';
  double noexp = strtod("1e+!", &end);
  int ok_noexp = noexp == 1.0 && *end == 'e';
  double noval = strtod(none, &end);
  int ok_noval = noval == 0.0 && end == none;
  double only_dot = strtod(dot, &end);
  int ok_only_dot = only_dot == 0.0 && end == dot;
  return ok_hex && ok_dot && ok_noexp && ok_noval && ok_only_dot ? 42 : 1;
}
SRC

cat > "$out_dir/getline_state.c" <<'SRC'
typedef void FILE;
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
unsigned long fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
long getline(char **lineptr, unsigned long *n, FILE *stream);
int feof(FILE *stream);
int main(void) {
  FILE *wf = fopen("tmp.txt", "w");
  fwrite("A\nBC", 1, 4, wf);
  fclose(wf);
  FILE *rf = fopen("tmp.txt", "r");
  char small[2];
  char *line = small;
  unsigned long cap = sizeof(small);
  long n1 = getline(&line, &cap, rf);
  int ok1 = n1 == 2 && line[0] == 'A' && line[1] == '\n' && line[2] == 0 && cap >= 3;
  char *line_after_grow = line;
  long n2 = getline(&line, &cap, rf);
  int ok2 = n2 == 2 && line == line_after_grow && line[0] == 'B' && line[1] == 'C' && line[2] == 0;
  long n3 = getline(&line, &cap, rf);
  int ok3 = n3 == -1 && feof(rf);
  fclose(rf);
  return ok1 && ok2 && ok3 ? 42 : 1;
}
SRC

cat > "$out_dir/localtime_state.c" <<'SRC'
typedef long time_t;
struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};
time_t time(time_t *tloc);
struct tm *localtime(time_t *timer);
double difftime(time_t end, time_t beginning);
int main(void) {
  time_t stored = -1;
  time_t now = time(&stored);
  struct tm *tm = localtime(&stored);
  return now == 0 && stored == 0 && tm != 0 &&
         tm->tm_sec == 0 && tm->tm_min == 0 && tm->tm_hour == 0 &&
         tm->tm_mday == 1 && tm->tm_mon == 0 && tm->tm_year == 70 &&
         tm->tm_wday == 4 && tm->tm_yday == 0 && tm->tm_isdst == 0 &&
         (int)difftime(10, 3) == 7 ? 42 : 1;
}
SRC

cat > "$out_dir/libc_runtime.c" <<'SRC'
typedef long va_list;
#define va_start(ap, last) ((void)(last), (ap) = (va_list)__va_arg_area)
#define va_arg(ap, type) (*(type *)((long)(ap += ((sizeof(type) + 7) & -8)) - ((sizeof(type) + 7) & -8)))
#define va_end(ap) ((void)(ap))
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
void qsort(void *base, long nmemb, long size, int (*compar)(void *, void *));
void *bsearch(void *key, void *base, long nmemb, long size, int (*compar)(void *, void *));
void exit(int status);
void abort(void);
char *getenv(char *name);
char *realpath(char *path, char *resolved_path);
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
struct stat {
  unsigned short st_mode;
  long st_size;
};
struct rusage {
  long ru_maxrss;
};
struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};
typedef long jmp_buf[48];
FILE *fopen(char *path, char *mode);
FILE *fdopen(int fd, char *mode);
int fclose(FILE *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
unsigned long fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int open(char *path, int oflag);
long read(int fd, void *buf, unsigned long count);
long write(int fd, void *buf, unsigned long count);
long lseek(int fd, long offset, int whence);
int close(int fd);
int fstat(int fd, struct stat *st);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
long getline(char **lineptr, unsigned long *n, FILE *stream);
int printf(char *fmt, ...);
int fprintf(FILE *stream, char *fmt, ...);
int vfprintf(FILE *stream, char *fmt, va_list ap);
int vsnprintf(char *buf, unsigned long size, char *fmt, va_list ap);
int puts(char *s);
int fputs(char *s, FILE *stream);
int fputc(int c, FILE *stream);
int fflush(FILE *stream);
void perror(char *s);
int getchar(void);
int getrusage(int who, struct rusage *usage);
struct tm *localtime(long *timer);
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
int call_vsnprintf(char *buf, unsigned long size, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}
int call_vfprintf(FILE *stream, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vfprintf(stream, fmt, ap);
  va_end(ap);
  return n;
}
int int_cmp(void *ap, void *bp) {
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
  char resolved_path[32];
  char *resolved_pathp = realpath("include", resolved_path);
  char *resolved_nullp = realpath("src", 0);
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
  long now = 0;
  struct tm *tm_info;
  struct rusage usage = {0};
  char *lineptr = 0;
  unsigned long linecap = 0;
  jmp_buf jb;
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
    longjmp(jb, 1);
  }
  int sj = setjmp(jb);
  int usage_ok = getrusage(0, &usage);
  tm_info = localtime(&now);
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
  long pos_after_write = ftell(wf);
  fclose(wf);
  int fd = open("tmp.txt", 0);
  struct stat st = {0, 0};
  int stat_ok = fstat(fd, &st);
  char fdbuf[4];
  long fdread = read(fd, fdbuf, 3);
  int close_ok = close(fd);
  int fd_a = open("tmp.txt", 0);
  int fd_b = open("tmp.txt", 0);
  char fda[2];
  char fdb[3];
  long fda1 = read(fd_a, fda, 1);
  long fdb2 = read(fd_b, fdb, 2);
  long fda2 = read(fd_a, fda + 1, 1);
  int close_a = close(fd_a);
  int close_b = close(fd_b);
  int fd_independent_ok = fda1 == 1 && fdb2 == 2 && fda2 == 1 &&
                          fda[0] == 'A' && fda[1] == '\n' &&
                          fdb[0] == 'A' && fdb[1] == '\n' &&
                          close_a == 0 && close_b == 0;
  int fdw = open("tmp.txt", 0);
  long fdw_pos0 = lseek(fdw, 0, 0);
  long fdw_written1 = write(fdw, "XYZ", 3);
  long fdw_pos1 = lseek(fdw, -2, 1);
  long fdw_written2 = write(fdw, "q", 1);
  long fdw_end = lseek(fdw, 0, 2);
  long fdw_pos2 = lseek(fdw, 0, 0);
  char fdwbuf[4];
  long fdw_read = read(fdw, fdwbuf, 3);
  int fdw_close = close(fdw);
  int fd_write_seek_ok = fdw_pos0 == 0 && fdw_written1 == 3 && fdw_pos1 == 1 &&
                         fdw_written2 == 1 && fdw_end == 3 && fdw_pos2 == 0 &&
                         fdw_read == 3 && fdwbuf[0] == 'X' && fdwbuf[1] == 'q' &&
                         fdwbuf[2] == 'Z' && fdw_close == 0;
  int fd_trunc = open("tmp.txt", 0x0400);
  struct stat st_trunc = {0, 0};
  int fstat_trunc_ok = fstat(fd_trunc, &st_trunc);
  long wrote_trunc = write(fd_trunc, "LM", 2);
  long pos_trunc = lseek(fd_trunc, 0, 2);
  int close_trunc = close(fd_trunc);
  int close_trunc_again = close(fd_trunc);
  int fd_append = open("tmp.txt", 0x0008);
  long append_pos = lseek(fd_append, 0, 1);
  long wrote_append = write(fd_append, "N", 1);
  long append_end = lseek(fd_append, 0, 2);
  long append_rewind = lseek(fd_append, 0, 0);
  char append_buf[4];
  long append_read = read(fd_append, append_buf, 3);
  int close_append = close(fd_append);
  int open_flags_ok = fstat_trunc_ok == 0 && st_trunc.st_size == 0 &&
                      wrote_trunc == 2 && pos_trunc == 2 && close_trunc == 0 &&
                      close_trunc_again == -1 && append_pos == 2 &&
                      wrote_append == 1 && append_end == 3 && append_rewind == 0 &&
                      append_read == 3 && append_buf[0] == 'L' &&
                      append_buf[1] == 'M' && append_buf[2] == 'N' &&
                      close_append == 0;
  FILE *wf_restore = fopen("tmp.txt", "w");
  fwrite("A\nB", 1, 3, wf_restore);
  fclose(wf_restore);
  int fd2 = open("tmp.txt", 0);
  FILE *fdstream = fdopen(fd2, "r");
  char fdline[4];
  char *fdlinep = fgets(fdline, sizeof(fdline), fdstream);
  char fdafter[2];
  long fd_after_stream = read(fd2, fdafter, 1);
  int fdopen_sync_ok = fd_after_stream == 1 && fdafter[0] == 'B';
  fclose(fdstream);
  close(fd2);
  int fdclose = open("tmp.txt", 0);
  FILE *fdclose_stream = fdopen(fdclose, "r");
  fclose(fdclose_stream);
  char fdclosed_buf[1];
  long fdclosed_read = read(fdclose, fdclosed_buf, 1);
  long fdclosed_write = write(fdclose, "x", 1);
  int fdclosed_close = close(fdclose);
  FILE *rfa = fopen("tmp.txt", "r");
  FILE *rfb = fopen("tmp.txt", "r");
  int rfa_ch1 = fgetc(rfa);
  char rfb_line[4];
  char *rfb_linep = fgets(rfb_line, sizeof(rfb_line), rfb);
  int rfa_ch2 = fgetc(rfa);
  int file_independent_ok = rfa_ch1 == 'A' && rfa_ch2 == '\n' &&
                            rfb_linep == rfb_line && rfb_line[0] == 'A' &&
                            rfb_line[1] == '\n' && rfb_line[2] == 0;
  fclose(rfa);
  fclose(rfb);
  unsigned long stdout_wrote = fwrite("NO", 1, 2, (FILE *)1);
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
  long getlinen = getline(&lineptr, &linecap, rf);
  int getline_ok = getlinen == 2 && lineptr[0] == 'A' && lineptr[1] == '\n' &&
                   lineptr[2] == 0 && linecap >= 3;
  fclose(rf);
  FILE *rf2 = fopen("tmp.txt", "r");
  char line[8];
  char *linep = fgets(line, sizeof(line), rf2);
  int ch2 = getc(rf2);
  fclose(rf2);
  FILE *ow = fopen("tmp.txt", "w");
  fwrite("ABC", 1, 3, ow);
  fseek(ow, 1, 0);
  unsigned long overwrote = fwrite("Z", 1, 1, ow);
  long pos_after_overwrite = ftell(ow);
  fclose(ow);
  FILE *ap = fopen("tmp.txt", "a");
  long append_start = ftell(ap);
  unsigned long appended = fwrite("D", 1, 1, ap);
  fclose(ap);
  FILE *orr = fopen("tmp.txt", "r");
  char owbuf[5];
  unsigned long owread = fread(owbuf, 1, 4, orr);
  int ow_eof = fgetc(orr);
  fclose(orr);
  int fdap = open("tmp.txt", 0);
  FILE *fdap_stream = fdopen(fdap, "a");
  long fdappend_start = ftell(fdap_stream);
  unsigned long fdappended = fwrite("E", 1, 1, fdap_stream);
  fclose(fdap_stream);
  close(fdap);
  FILE *fdapr = fopen("tmp.txt", "r");
  char fdapbuf[6];
  unsigned long fdapread = fread(fdapbuf, 1, 5, fdapr);
  fclose(fdapr);
  FILE *fpw = fopen("tmp.txt", "w");
  int fputs_file_ret = fputs("H", fpw);
  int fputc_file_ret = fputc('I', fpw);
  long pos_after_fputs = ftell(fpw);
  fclose(fpw);
  FILE *fpr = fopen("tmp.txt", "r");
  char fpbuf[3];
  unsigned long fpread = fread(fpbuf, 1, 2, fpr);
  int fp_eof = fgetc(fpr);
  fclose(fpr);
  FILE *fmtw = fopen("tmp.txt", "w");
  int fmt_file_ret = fprintf(fmtw, "K%d", 7);
  long fmt_file_pos = ftell(fmtw);
  fclose(fmtw);
  FILE *fmtr = fopen("tmp.txt", "r");
  char fmtbuf[3];
  unsigned long fmtread = fread(fmtbuf, 1, 2, fmtr);
  int fmt_eof = fgetc(fmtr);
  fclose(fmtr);
  char vfmtbuf[8];
  int vfmt_ret = call_vsnprintf(vfmtbuf, sizeof(vfmtbuf), "%d-%s", 31, "ok");
  FILE *vfmtw = fopen("tmp.txt", "w");
  int vfprintf_ret = call_vfprintf(vfmtw, "V%d", 5);
  long vfprintf_pos = ftell(vfmtw);
  fclose(vfmtw);
  int file_write_pos_ok = overwrote == 1 && pos_after_overwrite == 2 &&
                          append_start == 3 && appended == 1 &&
                          owread == 4 && owbuf[0] == 'A' && owbuf[1] == 'Z' &&
                          owbuf[2] == 'C' && owbuf[3] == 'D' && ow_eof == -1 &&
                          fdappend_start == 4 && fdappended == 1 &&
                          fdapread == 5 && fdapbuf[0] == 'A' && fdapbuf[1] == 'Z' &&
                          fdapbuf[2] == 'C' && fdapbuf[3] == 'D' && fdapbuf[4] == 'E' &&
                          fputs_file_ret == 1 && fputc_file_ret == 'I' &&
                          pos_after_fputs == 2 && fpread == 2 &&
                          fpbuf[0] == 'H' && fpbuf[1] == 'I' && fp_eof == -1;
  int fprintf_file_ok = fmt_file_ret == 2 && fmt_file_pos == 2 &&
                        fmtread == 2 && fmtbuf[0] == 'K' &&
                        fmtbuf[1] == '7' && fmt_eof == -1;
  int vformat_ok = vfmt_ret == 5 && vfmtbuf[0] == '3' && vfmtbuf[1] == '1' &&
                   vfmtbuf[2] == '-' && vfmtbuf[3] == 'o' && vfmtbuf[4] == 'k' &&
                   vfmtbuf[5] == 0 && vfprintf_ret == 2 && vfprintf_pos == 2;
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
         atexit(nullv) == 0 && getenv("AGC_MISSING_ENV") == 0 &&
         resolved_pathp == resolved_path && strcmp(resolved_path, "include") == 0 &&
         resolved_nullp != 0 && strcmp(resolved_nullp, "src") == 0 &&
         system("true") == 0 &&
         nums[0] == 1 && nums[1] == 2 && nums[2] == 3 && nums[3] == 4 && nums[4] == 5 &&
         found == nums + 2 && *found == 3 &&
         time(&tloc) == 0 && tloc == 0 && clock() == 0 &&
         (int)difftime(100, 58) == 42 &&
         tm_info != 0 && tm_info->tm_sec == 0 && tm_info->tm_min == 0 &&
         tm_info->tm_hour == 0 && tm_info->tm_mday == 1 && tm_info->tm_mon == 0 &&
         tm_info->tm_year == 70 && tm_info->tm_wday == 4 && tm_info->tm_yday == 0 &&
         usage_ok == 0 && usage.ru_maxrss == 0 &&
         getline_ok &&
         sj == 0 &&
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
         feclearexcept(31) == 0 && fegetexceptflag(&flag, 16) == 0 && flag == 0 &&
         feraiseexcept(4) == 0 && fesetexceptflag(&flag, 16) == 0 &&
         fetestexcept(31) == 4 &&
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
         wrote == 3 && pos_after_write == 3 &&
         stdout_wrote == 2 && readn == 2 && rb[0] == 'A' && rb[1] == '\n' && ch == 'B' &&
         fd >= 0 && stat_ok == 0 && (st.st_mode & 0170000) == 0100000 &&
         st.st_size == 3 && fdread == 3 && fdbuf[0] == 'A' && fdbuf[1] == '\n' &&
         fdbuf[2] == 'B' && close_ok == 0 &&
         fd_independent_ok &&
         fd_write_seek_ok &&
         open_flags_ok &&
         fdstream != 0 && fdlinep == fdline && fdline[0] == 'A' && fdline[1] == '\n' &&
         fdopen_sync_ok &&
         fdclosed_read == -1 && fdclosed_write == -1 && fdclosed_close == -1 &&
         file_independent_ok &&
         pos_after_read == 2 && !eof_after_ch && eof_read == -1 && eof_after_miss &&
         seek_ok == 0 && pos_after_seek == 1 && ch_seek == '\n' &&
         bad_seek == -1 && err_after_bad_seek && !err_after_clear && pos_after_rewind == 0 &&
         linep == line && line[0] == 'A' && line[1] == '\n' && line[2] == 0 &&
         ch2 == 'B' &&
         file_write_pos_ok &&
         fprintf_file_ok &&
         vformat_ok &&
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

{
  printf 'int main(void) {\n'
  printf '  int sum = 0;\n'
  for i in $(seq 0 299); do
    printf '  int x%d = %d;\n' "$i" "$i"
    printf '  sum = sum + x%d;\n' "$i"
  done
  printf '  return sum - 44808;\n'
  printf '}\n'
} > "$out_dir/many_locals.c"

"$root/build/ag_c_wasm" -c -o "$out_dir/main.o" "$out_dir/main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/other.o" "$out_dir/other.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked.wasm" \
  "$out_dir/main.o" "$out_dir/other.o"
wasm-validate "$out_dir/linked.wasm"
wasm-interp "$out_dir/linked.wasm" --run-all-exports > "$out_dir/linked.interp"
grep -q 'main() => i32:42' "$out_dir/linked.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/multi_export.o" "$out_dir/multi_export.c"
"$root/build/ag_wasm_link" --no-entry --export=main --export=answer -o "$out_dir/linked_multi_export.wasm" \
  "$out_dir/multi_export.o"
wasm-validate "$out_dir/linked_multi_export.wasm"
wasm-interp "$out_dir/linked_multi_export.wasm" --run-all-exports > "$out_dir/linked_multi_export.interp"
grep -q 'main() => i32:42' "$out_dir/linked_multi_export.interp"
grep -q 'answer() => i32:7' "$out_dir/linked_multi_export.interp"

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

"$root/build/ag_c_wasm" -c -o "$out_dir/indirect_aggregate_return_sig.o" \
  "$out_dir/indirect_aggregate_return_sig.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_indirect_aggregate_return_sig.wasm" \
  "$out_dir/indirect_aggregate_return_sig.o"
wasm-validate "$out_dir/linked_indirect_aggregate_return_sig.wasm"
wasm-interp "$out_dir/linked_indirect_aggregate_return_sig.wasm" --run-all-exports \
  > "$out_dir/linked_indirect_aggregate_return_sig.interp"
grep -q 'main() => i32:42' "$out_dir/linked_indirect_aggregate_return_sig.interp"

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

"$root/build/ag_c_wasm" -c -o "$out_dir/snprintf_float.o" "$out_dir/snprintf_float.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_snprintf_float.wasm" \
  "$out_dir/snprintf_float.o"
wasm-validate "$out_dir/linked_snprintf_float.wasm"
wasm-interp "$out_dir/linked_snprintf_float.wasm" --run-all-exports > "$out_dir/linked_snprintf_float.interp"
grep -q 'main() => i32:42' "$out_dir/linked_snprintf_float.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/vformat_file.o" "$out_dir/vformat_file.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_vformat_file.wasm" \
  "$out_dir/vformat_file.o"
wasm-validate "$out_dir/linked_vformat_file.wasm"
wasm-interp "$out_dir/linked_vformat_file.wasm" --run-all-exports > "$out_dir/linked_vformat_file.interp"
grep -q 'main() => i32:42' "$out_dir/linked_vformat_file.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/wide_locale_state.o" "$out_dir/wide_locale_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_wide_locale_state.wasm" \
  "$out_dir/wide_locale_state.o"
wasm-validate "$out_dir/linked_wide_locale_state.wasm"
wasm-interp "$out_dir/linked_wide_locale_state.wasm" --run-all-exports > "$out_dir/linked_wide_locale_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_wide_locale_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/fenv_state.o" "$out_dir/fenv_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fenv_state.wasm" \
  "$out_dir/fenv_state.o"
wasm-validate "$out_dir/linked_fenv_state.wasm"
wasm-interp "$out_dir/linked_fenv_state.wasm" --run-all-exports > "$out_dir/linked_fenv_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_fenv_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/signal_state.o" "$out_dir/signal_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_signal_state.wasm" \
  "$out_dir/signal_state.o"
wasm-validate "$out_dir/linked_signal_state.wasm"
wasm-interp "$out_dir/linked_signal_state.wasm" --run-all-exports > "$out_dir/linked_signal_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_signal_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/strto_base.o" "$out_dir/strto_base.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_strto_base.wasm" \
  "$out_dir/strto_base.o"
wasm-validate "$out_dir/linked_strto_base.wasm"
wasm-interp "$out_dir/linked_strto_base.wasm" --run-all-exports > "$out_dir/linked_strto_base.interp"
grep -q 'main() => i32:42' "$out_dir/linked_strto_base.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/strtod_state.o" "$out_dir/strtod_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_strtod_state.wasm" \
  "$out_dir/strtod_state.o"
wasm-validate "$out_dir/linked_strtod_state.wasm"
wasm-interp "$out_dir/linked_strtod_state.wasm" --run-all-exports > "$out_dir/linked_strtod_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_strtod_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/getline_state.o" "$out_dir/getline_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_getline_state.wasm" \
  "$out_dir/getline_state.o"
wasm-validate "$out_dir/linked_getline_state.wasm"
wasm-interp "$out_dir/linked_getline_state.wasm" --run-all-exports > "$out_dir/linked_getline_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_getline_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/localtime_state.o" "$out_dir/localtime_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_localtime_state.wasm" \
  "$out_dir/localtime_state.o"
wasm-validate "$out_dir/linked_localtime_state.wasm"
wasm-interp "$out_dir/linked_localtime_state.wasm" --run-all-exports > "$out_dir/linked_localtime_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_localtime_state.interp"

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
  grep -q '<env.vsnprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.vfprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  grep -q '<env.realpath>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.system>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.time>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.clock>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.difftime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.localtime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getrusage>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getline>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.setjmp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.longjmp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  grep -q '<env.fdopen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fclose>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fwrite>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fread>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.open>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.read>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.write>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lseek>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.close>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fstat>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fgetc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fgets>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  grep -q '<env.putchar>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  return __stdinp == 0 && __stdoutp == (FILE *)1 && __stderrp == (FILE *)2 ? 42 : 1;
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

"$root/build/ag_c_wasm" -c -o "$out_dir/many_locals.o" "$out_dir/many_locals.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_many_locals.wasm" \
  "$out_dir/many_locals.o"
wasm-validate "$out_dir/linked_many_locals.wasm"
wasm-interp "$out_dir/linked_many_locals.wasm" --run-all-exports > "$out_dir/linked_many_locals.interp"
grep -q 'main() => i32:42' "$out_dir/linked_many_locals.interp"

echo "ag_wasm_link smoke: ok"
