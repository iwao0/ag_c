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
  char o[32];
  char p[32];
  char q[32];
  char r[32];
  char s[32];
  char t[32];
  char u[32];
  char v[32];
  char w[32];
  char x[32];
  char y[32];
  char z[32];
  char aa[32];
  char ab[32];
  char ac[32];
  char ad[32];
  char ae[32];
  char af[32];
  char ag[32];
  char ah[32];
  char ai[32];
  char aj[32];
  char ak[32];
  char al[32];
  char am[32];
  char an[32];
  char ao[32];
  char ap[32];
  char aq[32];
  char ar[32];
  char as[32];
  char at[32];
  char au[32];
  char av[32];
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
  int no = snprintf(o, sizeof(o), "%-5s", "xy");
  int np = snprintf(p, sizeof(p), "%*s", -5, "xy");
  int nq = snprintf(q, sizeof(q), "%-3c", 'R');
  int nr = snprintf(r, sizeof(r), "%-5d", -7);
  int ns = snprintf(s, sizeof(s), "%-5u", 8u);
  int nt = snprintf(t, sizeof(t), "%-6.1f", 2.6);
  int nu = snprintf(u, sizeof(u), "%-05d", 7);
  int nv = snprintf(v, sizeof(v), "%x", 0x2au);
  int nw = snprintf(w, sizeof(w), "%X", 0x2au);
  int nx = snprintf(x, sizeof(x), "%04x", 0x2au);
  int ny = snprintf(y, sizeof(y), "%-5x", 0x2au);
  int nz = snprintf(z, sizeof(z), "%o", 0755u);
  int naa = snprintf(aa, sizeof(aa), "%p", (char *)0);
  int nab = snprintf(ab, sizeof(ab), "%-5p", (char *)0);
  long ldv = -2147483649L;
  unsigned long luv = 4294967295UL + 1UL;
  unsigned long lhv = 0x10000002aUL;
  unsigned long long llhv = 0x10000002aULL;
  int nac = snprintf(ac, sizeof(ac), "%ld", ldv);
  int nad = snprintf(ad, sizeof(ad), "%lu", luv);
  int nae = snprintf(ae, sizeof(ae), "%lx", lhv);
  int naf = snprintf(af, sizeof(af), "%llx", llhv);
  int nag = snprintf(ag, sizeof(ag), "%i", -17);
  int nah = snprintf(ah, sizeof(ah), "%.3d", 7);
  int nai = snprintf(ai, sizeof(ai), "%5.3d", 7);
  int naj = snprintf(aj, sizeof(aj), "%05.3d", 7);
  int nak = snprintf(ak, sizeof(ak), "%.0d", 0);
  int nal = snprintf(al, sizeof(al), "%.4x", 0x2au);
  int nam = snprintf(am, sizeof(am), "%-5.3d", 7);
  int nan = snprintf(an, sizeof(an), "%#x", 0x2au);
  int nao = snprintf(ao, sizeof(ao), "%#X", 0x2au);
  int nap = snprintf(ap, sizeof(ap), "%#o", 0755u);
  int naq = snprintf(aq, sizeof(aq), "%#08x", 0x2au);
  int nar = snprintf(ar, sizeof(ar), "%#.4o", 0755u);
  int nas = snprintf(as, sizeof(as), "%#.0o", 0u);
  int nat = snprintf(at, sizeof(at), "%+d", 7);
  int nau = snprintf(au, sizeof(au), "% d", 7);
  int nav = snprintf(av, sizeof(av), "%+05d", 7);
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
         nn == 1 && n[0] == '%' && n[1] == 0 &&
         no == 5 && o[0] == 'x' && o[1] == 'y' && o[2] == ' ' && o[4] == ' ' && o[5] == 0 &&
         np == 5 && p[0] == 'x' && p[1] == 'y' && p[2] == ' ' && p[4] == ' ' && p[5] == 0 &&
         nq == 3 && q[0] == 'R' && q[1] == ' ' && q[2] == ' ' && q[3] == 0 &&
         nr == 5 && r[0] == '-' && r[1] == '7' && r[2] == ' ' && r[4] == ' ' && r[5] == 0 &&
         ns == 5 && s[0] == '8' && s[1] == ' ' && s[4] == ' ' && s[5] == 0 &&
         nt == 6 && t[0] == '2' && t[1] == '.' && t[2] == '6' && t[3] == ' ' && t[5] == ' ' && t[6] == 0 &&
         nu == 5 && u[0] == '7' && u[1] == ' ' && u[4] == ' ' && u[5] == 0 &&
         nv == 2 && v[0] == '2' && v[1] == 'a' && v[2] == 0 &&
         nw == 2 && w[0] == '2' && w[1] == 'A' && w[2] == 0 &&
         nx == 4 && x[0] == '0' && x[1] == '0' && x[2] == '2' && x[3] == 'a' && x[4] == 0 &&
         ny == 5 && y[0] == '2' && y[1] == 'a' && y[2] == ' ' && y[4] == ' ' && y[5] == 0 &&
         nz == 3 && z[0] == '7' && z[1] == '5' && z[2] == '5' && z[3] == 0 &&
         naa == 3 && aa[0] == '0' && aa[1] == 'x' && aa[2] == '0' && aa[3] == 0 &&
         nab == 5 && ab[0] == '0' && ab[1] == 'x' && ab[2] == '0' && ab[3] == ' ' && ab[4] == ' ' && ab[5] == 0 &&
         nac == 11 && ac[0] == '-' && ac[1] == '2' && ac[10] == '9' && ac[11] == 0 &&
         nad == 10 && ad[0] == '4' && ad[9] == '6' && ad[10] == 0 &&
         nae == 9 && ae[0] == '1' && ae[1] == '0' && ae[8] == 'a' && ae[9] == 0 &&
         naf == 9 && af[0] == '1' && af[1] == '0' && af[8] == 'a' && af[9] == 0 &&
         nag == 3 && ag[0] == '-' && ag[1] == '1' && ag[2] == '7' && ag[3] == 0 &&
         nah == 3 && ah[0] == '0' && ah[1] == '0' && ah[2] == '7' && ah[3] == 0 &&
         nai == 5 && ai[0] == ' ' && ai[1] == ' ' && ai[2] == '0' && ai[4] == '7' && ai[5] == 0 &&
         naj == 5 && aj[0] == ' ' && aj[1] == ' ' && aj[2] == '0' && aj[4] == '7' && aj[5] == 0 &&
         nak == 0 && ak[0] == 0 &&
         nal == 4 && al[0] == '0' && al[1] == '0' && al[2] == '2' && al[3] == 'a' && al[4] == 0 &&
         nam == 5 && am[0] == '0' && am[1] == '0' && am[2] == '7' && am[3] == ' ' && am[4] == ' ' && am[5] == 0 &&
         nan == 4 && an[0] == '0' && an[1] == 'x' && an[2] == '2' && an[3] == 'a' && an[4] == 0 &&
         nao == 4 && ao[0] == '0' && ao[1] == 'X' && ao[2] == '2' && ao[3] == 'A' && ao[4] == 0 &&
         nap == 4 && ap[0] == '0' && ap[1] == '7' && ap[2] == '5' && ap[3] == '5' && ap[4] == 0 &&
         naq == 8 && aq[0] == '0' && aq[1] == 'x' && aq[2] == '0' && aq[3] == '0' &&
         aq[4] == '0' && aq[5] == '0' && aq[6] == '2' && aq[7] == 'a' && aq[8] == 0 &&
         nar == 4 && ar[0] == '0' && ar[1] == '7' && ar[2] == '5' && ar[3] == '5' && ar[4] == 0 &&
         nas == 1 && as[0] == '0' && as[1] == 0 &&
         nat == 2 && at[0] == '+' && at[1] == '7' && at[2] == 0 &&
         nau == 2 && au[0] == ' ' && au[1] == '7' && au[2] == 0 &&
         nav == 5 && av[0] == '+' && av[1] == '0' && av[2] == '0' &&
         av[3] == '0' && av[4] == '7' && av[5] == 0 ? 42 : 1;
}
SRC

cat > "$out_dir/snprintf_length_mods.c" <<'SRC'
int snprintf(char *s, unsigned long n, const char *fmt, ...);
int main(void) {
  char a[48];
  char b[48];
  char c[32];
  signed char hhn = 0;
  short hn = 0;
  int in = 0;
  long ln = 0;
  long long lln = 0;
  long long jn = 0;
  long tn = 0;
  int na = snprintf(a, sizeof(a), "%hhd:%hhu:%hd:%hu:%#hhx:%#hx",
                    130, 250, -12345, 60000, 0x12a, 0x1234a);
  int nb = snprintf(b, sizeof(b), "%jd:%td:%zu",
                    (long long)-2147483649LL, (long)-9, (unsigned long)99);
  int nc = snprintf(c, sizeof(c), "ab%hhncd%hnEF%nG%lnH%llnI%jnJ%tn",
                    &hhn, &hn, &in, &ln, &lln, &jn, &tn);
  if (na != 33 || a[0] != '-' || a[1] != '1' || a[2] != '2' ||
      a[3] != '6' || a[4] != ':' || a[5] != '2' || a[6] != '5' ||
      a[7] != '0' || a[8] != ':' || a[9] != '-' || a[10] != '1' ||
      a[14] != '5' || a[15] != ':' || a[16] != '6' || a[21] != ':' ||
      a[22] != '0' || a[23] != 'x' || a[24] != '2' || a[25] != 'a' ||
      a[26] != ':' || a[27] != '0' || a[28] != 'x' || a[29] != '2' ||
      a[32] != 'a' || a[33] != 0) return 1;
  if (nb != 17 || b[0] != '-' || b[1] != '2' || b[10] != '9' ||
      b[11] != ':' || b[12] != '-' || b[13] != '9' || b[14] != ':' ||
      b[15] != '9' || b[16] != '9' || b[17] != 0) return 2;
  if (nc != 10 || c[0] != 'a' || c[1] != 'b' || c[2] != 'c' ||
      c[3] != 'd' || c[4] != 'E' || c[5] != 'F' || c[6] != 'G' ||
      c[7] != 'H' || c[8] != 'I' || c[9] != 'J' || c[10] != 0) return 3;
  if (hhn != 2 || hn != 4 || in != 6 || ln != 7 || lln != 8 ||
      jn != 9 || tn != 10) return 4;
  return 42;
}
SRC

cat > "$out_dir/snprintf_float.c" <<'SRC'
int snprintf(char *s, unsigned long n, const char *fmt, ...);
static int zeros(char *s, int first, int last) {
  int i;
  for (i = first; i <= last; i++) {
    if (s[i] != '0') return 0;
  }
  return 1;
}
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
  char o[32];
  char p[32];
  char q[32];
  char r[32];
  char s[32];
  char t[32];
  char u[32];
  char v[32];
  char w[32];
  char x[32];
  char y[32];
  char z[32];
  char aa[32];
  char ab[32];
  char ac[32];
  char ad[32];
  char ae[32];
  char af[32];
  char ag[32];
  char ah[32];
  char ai[32];
  char aj[32];
  char ak[32];
  double zero = 0.0;
  double negzero = -zero;
  double inf = 1.0 / zero;
  double nanv = zero / zero;
  int na = snprintf(a, sizeof(a), "%.1f", 3.14);
  int nb = snprintf(b, sizeof(b), "%.2f", -2.25);
  int nc = snprintf(c, sizeof(c), "%.1Lf", (long double)4.04);
  int nd = snprintf(d, sizeof(d), "%6.1f", 3.14);
  int ne = snprintf(e, sizeof(e), "%06.1f", -2.34);
  int nf = snprintf(f, sizeof(f), "%6f", inf);
  int ng = snprintf(g, sizeof(g), "%F", -inf);
  int nh = snprintf(h, sizeof(h), "%f", nanv);
  int ni = snprintf(i, sizeof(i), "%.2e", 1234.0);
  int nj = snprintf(j, sizeof(j), "%10.1E", -0.0123);
  int nk = snprintf(k, sizeof(k), "%010.1e", 9.99);
  int nl = snprintf(l, sizeof(l), "%.4g", 1234.0);
  int nm = snprintf(m, sizeof(m), "%.3g", 0.0001234);
  int nn = snprintf(n, sizeof(n), "%8.2G", 12345.0);
  int no = snprintf(o, sizeof(o), "%.1a", 3.0);
  int np = snprintf(p, sizeof(p), "%.1A", -0.5);
  int nq = snprintf(q, sizeof(q), "%08.0a", 1.0);
  int nr = snprintf(r, sizeof(r), "%#.0f", 3.0);
  int ns = snprintf(s, sizeof(s), "%#.0e", 12.0);
  int nt = snprintf(t, sizeof(t), "%#.3g", 123.0);
  int nu = snprintf(u, sizeof(u), "%#.0a", 1.0);
  int nv = snprintf(v, sizeof(v), "%+.1f", 3.14);
  int nw = snprintf(w, sizeof(w), "% .1f", 3.14);
  int nx = snprintf(x, sizeof(x), "%+08.1f", 3.14);
  int ny = snprintf(y, sizeof(y), "%+.1e", 12.0);
  int nz = snprintf(z, sizeof(z), "%+.3g", 123.0);
  int naa = snprintf(aa, sizeof(aa), "%+.0a", 1.0);
  int nab = snprintf(ab, sizeof(ab), "%.1f", negzero);
  int nac = snprintf(ac, sizeof(ac), "%.1e", negzero);
  int nad = snprintf(ad, sizeof(ad), "%.1g", negzero);
  int nae = snprintf(ae, sizeof(ae), "%.0a", negzero);
  int naf = snprintf(af, sizeof(af), "%.1g", 9.9);
  int nag = snprintf(ag, sizeof(ag), "%.2g", 99.9);
  int nah = snprintf(ah, sizeof(ah), "%.1g", 0.00009999);
  int nai = snprintf(ai, sizeof(ai), "%.12f", 1.0);
  int naj = snprintf(aj, sizeof(aj), "%.12e", 1.0);
  int nak = snprintf(ak, sizeof(ak), "%#.12g", 1.0);
  return na == 3 && a[0] == '3' && a[1] == '.' && a[2] == '1' && a[3] == 0 &&
         nb == 5 && b[0] == '-' && b[1] == '2' && b[2] == '.' && b[3] == '2' &&
         b[4] == '5' && b[5] == 0 &&
         nc == 3 && c[0] == '4' && c[1] == '.' && c[2] == '0' && c[3] == 0 &&
         nd == 6 && d[0] == ' ' && d[1] == ' ' && d[2] == ' ' &&
         d[3] == '3' && d[4] == '.' && d[5] == '1' && d[6] == 0 &&
         ne == 6 && e[0] == '-' && e[1] == '0' && e[2] == '0' &&
         e[3] == '2' && e[4] == '.' && e[5] == '3' && e[6] == 0 &&
         nf == 6 && f[0] == ' ' && f[1] == ' ' && f[2] == ' ' &&
         f[3] == 'i' && f[4] == 'n' && f[5] == 'f' && f[6] == 0 &&
         ng == 4 && g[0] == '-' && g[1] == 'I' && g[2] == 'N' && g[3] == 'F' && g[4] == 0 &&
         nh == 3 && h[0] == 'n' && h[1] == 'a' && h[2] == 'n' && h[3] == 0 &&
         ni == 8 && i[0] == '1' && i[1] == '.' && i[2] == '2' && i[3] == '3' &&
         i[4] == 'e' && i[5] == '+' && i[6] == '0' && i[7] == '3' && i[8] == 0 &&
         nj == 10 && j[0] == ' ' && j[1] == ' ' && j[2] == '-' && j[3] == '1' &&
         j[4] == '.' && j[5] == '2' && j[6] == 'E' && j[7] == '-' &&
         j[8] == '0' && j[9] == '2' && j[10] == 0 &&
         nk == 10 && k[0] == '0' && k[1] == '0' && k[2] == '0' && k[3] == '1' &&
         k[4] == '.' && k[5] == '0' && k[6] == 'e' && k[7] == '+' &&
         k[8] == '0' && k[9] == '1' && k[10] == 0 &&
         nl == 4 && l[0] == '1' && l[1] == '2' && l[2] == '3' && l[3] == '4' && l[4] == 0 &&
         nm == 8 && m[0] == '0' && m[1] == '.' && m[2] == '0' && m[3] == '0' &&
         m[4] == '0' && m[5] == '1' && m[6] == '2' && m[7] == '3' && m[8] == 0 &&
         nn == 8 && n[0] == ' ' && n[1] == '1' && n[2] == '.' && n[3] == '2' &&
         n[4] == 'E' && n[5] == '+' && n[6] == '0' && n[7] == '4' && n[8] == 0 &&
         no == 8 && o[0] == '0' && o[1] == 'x' && o[2] == '1' && o[3] == '.' &&
         o[4] == '8' && o[5] == 'p' && o[6] == '+' && o[7] == '1' && o[8] == 0 &&
         np == 9 && p[0] == '-' && p[1] == '0' && p[2] == 'X' && p[3] == '1' &&
         p[4] == '.' && p[5] == '0' && p[6] == 'P' && p[7] == '-' &&
         p[8] == '1' && p[9] == 0 &&
         nq == 8 && q[0] == '0' && q[1] == '0' && q[2] == '0' && q[3] == 'x' &&
         q[4] == '1' && q[5] == 'p' && q[6] == '+' && q[7] == '0' && q[8] == 0 &&
         nr == 2 && r[0] == '3' && r[1] == '.' && r[2] == 0 &&
         ns == 6 && s[0] == '1' && s[1] == '.' && s[2] == 'e' && s[3] == '+' &&
         s[4] == '0' && s[5] == '1' && s[6] == 0 &&
         nt == 4 && t[0] == '1' && t[1] == '2' && t[2] == '3' && t[3] == '.' && t[4] == 0 &&
         nu == 7 && u[0] == '0' && u[1] == 'x' && u[2] == '1' && u[3] == '.' &&
         u[4] == 'p' && u[5] == '+' && u[6] == '0' && u[7] == 0 &&
         nv == 4 && v[0] == '+' && v[1] == '3' && v[2] == '.' && v[3] == '1' && v[4] == 0 &&
         nw == 4 && w[0] == ' ' && w[1] == '3' && w[2] == '.' && w[3] == '1' && w[4] == 0 &&
         nx == 8 && x[0] == '+' && x[1] == '0' && x[2] == '0' && x[3] == '0' &&
         x[4] == '0' && x[5] == '3' && x[6] == '.' && x[7] == '1' && x[8] == 0 &&
         ny == 8 && y[0] == '+' && y[1] == '1' && y[2] == '.' && y[3] == '2' &&
         y[4] == 'e' && y[5] == '+' && y[6] == '0' && y[7] == '1' && y[8] == 0 &&
         nz == 4 && z[0] == '+' && z[1] == '1' && z[2] == '2' && z[3] == '3' && z[4] == 0 &&
         naa == 7 && aa[0] == '+' && aa[1] == '0' && aa[2] == 'x' && aa[3] == '1' &&
         aa[4] == 'p' && aa[5] == '+' && aa[6] == '0' && aa[7] == 0 &&
         nab == 4 && ab[0] == '-' && ab[1] == '0' && ab[2] == '.' &&
         ab[3] == '0' && ab[4] == 0 &&
         nac == 8 && ac[0] == '-' && ac[1] == '0' && ac[2] == '.' &&
         ac[3] == '0' && ac[4] == 'e' && ac[5] == '+' && ac[6] == '0' &&
         ac[7] == '0' && ac[8] == 0 &&
         nad == 2 && ad[0] == '-' && ad[1] == '0' && ad[2] == 0 &&
         nae == 7 && ae[0] == '-' && ae[1] == '0' && ae[2] == 'x' &&
         ae[3] == '0' && ae[4] == 'p' && ae[5] == '+' && ae[6] == '0' &&
         ae[7] == 0 &&
         naf == 5 && af[0] == '1' && af[1] == 'e' && af[2] == '+' &&
         af[3] == '0' && af[4] == '1' && af[5] == 0 &&
         nag == 5 && ag[0] == '1' && ag[1] == 'e' && ag[2] == '+' &&
         ag[3] == '0' && ag[4] == '2' && ag[5] == 0 &&
         nah == 6 && ah[0] == '0' && ah[1] == '.' && ah[2] == '0' &&
         ah[3] == '0' && ah[4] == '0' && ah[5] == '1' && ah[6] == 0 &&
         nai == 14 && ai[0] == '1' && ai[1] == '.' && zeros(ai, 2, 13) && ai[14] == 0 &&
         naj == 18 && aj[0] == '1' && aj[1] == '.' && zeros(aj, 2, 13) &&
         aj[14] == 'e' && aj[15] == '+' && aj[16] == '0' && aj[17] == '0' &&
         aj[18] == 0 &&
         nak == 13 && ak[0] == '1' && ak[1] == '.' && zeros(ak, 2, 12) &&
         ak[13] == 0 ? 42 : 1;
}
SRC

cat > "$out_dir/vformat_file.c" <<'SRC'
typedef long va_list;
#define va_start(ap, last) ((void)(last), (ap) = (va_list)__va_arg_area)
#define va_end(ap) ((void)(ap))
typedef void FILE;
int vsnprintf(char *buf, unsigned long size, char *fmt, va_list ap);
int vsprintf(char *buf, char *fmt, va_list ap);
int vprintf(char *fmt, va_list ap);
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
int call_vsprintf(char *buf, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsprintf(buf, fmt, ap);
  va_end(ap);
  return n;
}
int call_vprintf(char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vprintf(fmt, ap);
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
  char sbuf[8];
  int spn = call_vsprintf(sbuf, "%d-%s", 12, "go");
  int pn = call_vprintf("P%d", 6);
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
         spn == 5 && sbuf[0] == '1' && sbuf[1] == '2' &&
         sbuf[2] == '-' && sbuf[3] == 'g' && sbuf[4] == 'o' &&
         sbuf[5] == 0 && pn == 2 &&
         fn == 2 && pos == 2 && got == 2 &&
         rb[0] == 'V' && rb[1] == '5' && eof == -1 ? 42 : 1;
}
SRC

cat > "$out_dir/vscan_state.c" <<'SRC'
typedef long va_list;
#define va_start(ap, last) ((void)(last), (ap) = (va_list)__va_arg_area)
#define va_end(ap) ((void)(ap))
typedef void FILE;
int vsscanf(char *s, char *fmt, va_list ap);
int vfscanf(FILE *stream, char *fmt, va_list ap);
int vscanf(char *fmt, va_list ap);
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
int fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int call_vsscanf(char *s, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsscanf(s, fmt, ap);
  va_end(ap);
  return n;
}
int call_vfscanf(FILE *stream, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vfscanf(stream, fmt, ap);
  va_end(ap);
  return n;
}
int call_vscanf(char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vscanf(fmt, ap);
  va_end(ap);
  return n;
}
int main(void) {
  int a = 0;
  unsigned int x = 0;
  char s[4];
  char c = 0;
  int n = 0;
  int r = call_vsscanf(" -11 1f uvwR", "%d %x %3s%c%n", &a, &x, s, &c, &n);
  if (r != 4 || a != -11 || x != 31) return 1;
  if (s[0] != 'u' || s[1] != 'v' || s[2] != 'w' || s[3] != 0) return 2;
  if (c != 'R' || n != 12) return 3;

  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 4;
  if (fwrite("22 2a okZ", 1, 9, wf) != 9 || fclose(wf) != 0) return 5;
  FILE *rf = fopen("tmp.txt", "r+");
  if (!rf) return 6;
  a = 0;
  x = 0;
  s[0] = s[1] = s[2] = s[3] = 0;
  c = 0;
  n = 0;
  r = call_vfscanf(rf, "%d %x %2s%c%n", &a, &x, s, &c, &n);
  if (fclose(rf) != 0) return 7;
  if (r != 4 || a != 22 || x != 42) return 8;
  if (s[0] != 'o' || s[1] != 'k' || s[2] != 0) return 9;
  if (c != 'Z' || n != 9) return 10;

  return 42;
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

cat > "$out_dir/locale_state.c" <<'SRC'
struct lconv {
  char *decimal_point;
};
char *setlocale(int category, char *locale);
struct lconv *localeconv(void);
int main(void) {
  char *query = setlocale(0, 0);
  char *c = setlocale(0, "C");
  char *native = setlocale(0, "");
  char *bad_name = setlocale(0, "ja_JP.UTF-8");
  char *bad_category = setlocale(99, "C");
  struct lconv *lc = localeconv();
  return query != 0 && query[0] == 'C' && query[1] == 0 &&
         c != 0 && c[0] == 'C' && c[1] == 0 &&
         native != 0 && native[0] == 'C' && native[1] == 0 &&
         bad_name == 0 && bad_category == 0 &&
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
  ok = ok && fesetround(0x12345678) != 0 && fegetround() == 0x00400000;
  ok = ok && fesetround(0x00800000) == 0 && fegetround() == 0x00800000;
  ok = ok && fesetround(0x00C00000) == 0 && fegetround() == 0x00C00000;
  ok = ok && fesetround(0) == 0 && fegetround() == 0;
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
  sig_handler_t bad_sig_hi = signal(99, handler);
  sig_handler_t bad_sig_neg = signal(-1, handler);
  return old == 0 && rc == 0 && seen == 32 &&
         old2 == handler && rc2 == 0 && seen == 32 && bad == -1 &&
         bad_sig_hi == (sig_handler_t)-1 && bad_sig_neg == (sig_handler_t)-1 ? 42 : 1;
}
SRC

cat > "$out_dir/atexit_state.c" <<'SRC'
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
int seen;
void handler(void) {
  seen = seen + 1;
}
int main(void) {
  int ok = 1;
  int i = 0;
  ok = ok && atexit(0) == 0;
  while (i < 32) {
    ok = ok && atexit(handler) == 0;
    i++;
  }
  ok = ok && seen == 0;
  ok = ok && atexit(handler) == -1;
  ok = ok && at_quick_exit(0) == 0;
  i = 0;
  while (i < 32) {
    ok = ok && at_quick_exit(handler) == 0;
    i++;
  }
  ok = ok && at_quick_exit(handler) == -1;
  return ok ? 42 : 1;
}
SRC

cat > "$out_dir/strto_base.c" <<'SRC'
long strtol(char *s, char **endptr, int base);
unsigned long strtoul(char *s, char **endptr, int base);
unsigned long strtoumax(char *s, char **endptr, int base);
int main(void) {
  char *end = 0;
  char *none = "xyz";
  char *bad_src = "10!";
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
  long b36 = strtol("z!", &end, 36);
  int ok_b36 = b36 == 35 && *end == '!';
  long bad_base_hi = strtol(bad_src, &end, 37);
  int ok_bad_hi = bad_base_hi == 0 && end == bad_src;
  unsigned long bad_base_lo = strtoul(bad_src, &end, 1);
  int ok_bad_lo = bad_base_lo == 0 && end == bad_src;
  return ok_hex && ok_oct && ok_ux && ok_uneg && ok_none &&
         ok_b36 && ok_bad_hi && ok_bad_lo ? 42 : 1;
}
SRC

cat > "$out_dir/strtod_state.c" <<'SRC'
float strtof(char *s, char **endptr);
double strtod(char *s, char **endptr);
long double strtold(char *s, char **endptr);
int *__error(void);
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
  float f = strtof(" .5!", &end);
  int ok_float = (int)(f * 100.0f) == 50 && *end == '!';
  long double ld = strtold("0x1.4p+2!", &end);
  int ok_long_double = (int)(ld * 10.0L) == 50 && *end == '!';
  int *errp = __error();
  *errp = 0;
  double ov = strtod("1e309!", &end);
  int ok_ov = ov > 1e300 && *end == '!' && *errp == 34;
  *errp = 0;
  double nov = strtod("-1e309!", &end);
  int ok_nov = nov < -1e300 && *end == '!' && *errp == 34;
  *errp = 0;
  double under = strtod("1e-400!", &end);
  int ok_under = under == 0.0 && *end == '!' && *errp == 34;
  *errp = 0;
  double hex_under = strtod("0x1p-2000!", &end);
  int ok_hex_under = hex_under == 0.0 && *end == '!' && *errp == 34;
  *errp = 0;
  double zero_huge = strtod("0e9999!", &end);
  int ok_zero_huge = zero_huge == 0.0 && *end == '!' && *errp == 0;
  *errp = 0;
  double infv = strtod("INF!", &end);
  int ok_inf = infv > 1e300 && *end == '!' && *errp == 0;
  double ninfv = strtof("-infinity!", &end);
  int ok_ninf = ninfv < -1e300 && *end == '!' && *errp == 0;
  double nanv = strtod("nan(payload)!", &end);
  int ok_nan = nanv != 0.0 && !(nanv < 0.0) && !(nanv > 0.0) && *end == '!' && *errp == 0;
  long double ldn = strtold("NaN(open!", &end);
  int ok_nan_open = ldn != 0.0L && !(ldn < 0.0L) && !(ldn > 0.0L) && *end == '(' && *errp == 0;
  return ok_hex && ok_dot && ok_noexp && ok_noval && ok_only_dot &&
         ok_float && ok_long_double && ok_ov && ok_nov && ok_under &&
         ok_hex_under && ok_zero_huge && ok_inf && ok_ninf && ok_nan &&
         ok_nan_open ? 42 : 1;
}
SRC

cat > "$out_dir/getline_state.c" <<'SRC'
typedef void FILE;
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
unsigned long fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int fgetc(FILE *stream);
int ungetc(int c, FILE *stream);
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
  FILE *rf2 = fopen("tmp.txt", "r");
  int ch = fgetc(rf2);
  int pushed = ungetc('Z', rf2);
  long n4 = getline(&line, &cap, rf2);
  int ok4 = ch == 'A' && pushed == 'Z' && n4 == 2 &&
            line[0] == 'Z' && line[1] == '\n' && line[2] == 0;
  fclose(rf2);
  return ok1 && ok2 && ok3 && ok4 ? 42 : 1;
}
SRC

cat > "$out_dir/stdio_size_state.c" <<'SRC'
typedef void FILE;
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
unsigned long fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
int main(void) {
  unsigned long huge = 1UL << 63;
  FILE *wf = fopen("tmp.txt", "w");
  unsigned long wbad1 = fwrite("x", huge, 2, wf);
  unsigned long wbad2 = fwrite("x", (unsigned long)-1, 1, wf);
  long pos_bad = ftell(wf);
  unsigned long wok = fwrite("AB", 1, 2, wf);
  long pos_ok = ftell(wf);
  fclose(wf);
  FILE *rf = fopen("tmp.txt", "r");
  char buf[4];
  unsigned long rbad = fread(buf, huge, 2, rf);
  long rpos_bad = ftell(rf);
  int eof_bad = feof(rf);
  unsigned long rok = fread(buf, 1, 2, rf);
  long rpos_ok = ftell(rf);
  int eof_ok = feof(rf);
  fclose(rf);
  return wbad1 == 0 && wbad2 == 0 && pos_bad == 0 &&
         wok == 2 && pos_ok == 2 &&
         rbad == 0 && rpos_bad == 0 && !eof_bad &&
         rok == 2 && buf[0] == 'A' && buf[1] == 'B' &&
         rpos_ok == 2 && !eof_ok ? 42 : 1;
}
SRC

cat > "$out_dir/stdio_invalid_state.c" <<'SRC'
typedef void FILE;
FILE *fopen(char *path, char *mode);
FILE *freopen(char *path, char *mode, FILE *stream);
FILE *fdopen(int fd, char *mode);
int fclose(FILE *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
unsigned long fwrite(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
int fputs(char *s, FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int open(char *path, int oflag);
int close(int fd);
int main(void) {
  int fd = open("tmp.txt", 0);
  int fd2 = open("tmp.txt", 0);
  FILE *ok = fdopen(fd, "r");
  int bad_path = fopen(0, "r") == 0;
  int bad_mode_null = fopen("tmp.txt", 0) == 0;
  int bad_mode_empty = fopen("tmp.txt", "") == 0;
  int bad_mode_unknown = fopen("tmp.txt", "z") == 0;
  int bad_fd = fdopen(-1, "r") == 0;
  int bad_fd_mode_null = fdopen(fd2, 0) == 0;
  int bad_fd_mode_unknown = fdopen(fd2, "z") == 0;
  int ok_stream = ok != 0;
  if (ok) fclose(ok);
  close(fd2);
  FILE *rf = fopen("tmp.txt", "r");
  int read_stream_write = fwrite("x", 1, 1, rf) == 0 &&
                          fputs("x", rf) == -1 &&
                          fputc('x', rf) == -1;
  int read_stream_error = ferror(rf);
  clearerr(rf);
  int read_stream_clear = ferror(rf);
  fclose(rf);
  FILE *wf = fopen("tmp.txt", "w");
  char b[1];
  fwrite("A", 1, 1, wf);
  fseek(wf, 0, 0);
  int write_stream_read = fread(b, 1, 1, wf) == 0 && fgetc(wf) == -1;
  int write_stream_error = ferror(wf);
  clearerr(wf);
  int write_stream_clear = ferror(wf);
  fclose(wf);
  return bad_path && bad_mode_null && bad_mode_empty && bad_mode_unknown &&
         bad_fd && bad_fd_mode_null && bad_fd_mode_unknown && ok_stream &&
         read_stream_write && read_stream_error && !read_stream_clear &&
         write_stream_read && write_stream_error && !write_stream_clear ? 42 : 1;
}
SRC

cat > "$out_dir/remove_state.c" <<'SRC'
typedef void FILE;
#define EOF (-1)
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
int remove(char *path);
int rename(char *oldpath, char *newpath);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
int feof(FILE *stream);
int main(void) {
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 1;
  if (fputc('A', wf) != 'A' || fputc('B', wf) != 'B') return 2;
  if (fclose(wf) != 0) return 3;
  if (remove(0) == 0) return 4;
  if (remove("tmp.txt") != 0) return 5;
  FILE *rf = fopen("tmp.txt", "r");
  if (!rf) return 6;
  if (fgetc(rf) != EOF || !feof(rf)) return 7;
  if (fclose(rf) != 0) return 8;
  FILE *wf2 = fopen("tmp.txt", "w");
  if (!wf2) return 9;
  if (fputc('Z', wf2) != 'Z' || fclose(wf2) != 0) return 10;
  FILE *rf2 = fopen("tmp.txt", "r");
  if (!rf2) return 11;
  if (fgetc(rf2) != 'Z') return 12;
  if (fclose(rf2) != 0) return 13;
  if (rename(0, "new.txt") == 0) return 14;
  if (rename("tmp.txt", 0) == 0) return 15;
  if (rename("tmp.txt", "new.txt") != 0) return 16;
  FILE *rf3 = fopen("new.txt", "r");
  if (!rf3) return 17;
  if (fgetc(rf3) != 'Z') return 18;
  if (fclose(rf3) != 0) return 19;
  return 42;
}
SRC

cat > "$out_dir/freopen_state.c" <<'SRC'
typedef void FILE;
#define SEEK_SET 0
#define EOF (-1)
FILE *fopen(char *path, char *mode);
FILE *freopen(char *path, char *mode, FILE *stream);
int fclose(FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
int feof(FILE *stream);
int main(void) {
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 1;
  if (fputc('A', wf) != 'A' || fputc('B', wf) != 'B' || fclose(wf) != 0) return 2;
  FILE *rf = fopen("tmp.txt", "r");
  if (!rf) return 3;
  if (fgetc(rf) != 'A') return 4;
  if (freopen(0, "r", rf) != 0) return 5;
  if (freopen("tmp.txt", 0, rf) != 0) return 6;
  if (freopen("tmp.txt", "z", rf) != 0) return 7;
  if (freopen("tmp.txt", "w+", rf) != rf) return 8;
  if (fputc('Z', rf) != 'Z') return 9;
  if (fseek(rf, 0, SEEK_SET) != 0) return 10;
  if (fgetc(rf) != 'Z') return 11;
  if (fgetc(rf) != EOF || !feof(rf)) return 12;
  if (freopen("tmp.txt", "r", rf) != rf) return 13;
  if (fgetc(rf) != 'Z') return 14;
  if (fclose(rf) != 0) return 15;
  return 42;
}
SRC

cat > "$out_dir/ungetc_state.c" <<'SRC'
typedef void FILE;
#define EOF (-1)
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int ungetc(int c, FILE *stream);
int fscanf(FILE *stream, char *fmt, ...);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int feof(FILE *stream);
int main(void) {
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 1;
  if (fputc('A', wf) != 'A' || fputc('B', wf) != 'B' ||
      fputc('\n', wf) != '\n' || fputc('C', wf) != 'C') return 2;
  if (fclose(wf) != 0) return 3;

  FILE *rf = fopen("tmp.txt", "r");
  if (!rf) return 4;
  if (fgetc(rf) != 'A') return 5;
  if (ungetc('X', rf) != 'X') return 6;
  if (fgetc(rf) != 'X') return 7;
  if (getc(rf) != 'B') return 8;
  if (ungetc('Y', rf) != 'Y') return 9;
  char buf[3];
  if (fread(buf, 1, 3, rf) != 3) return 10;
  if (buf[0] != 'Y' || buf[1] != '\n' || buf[2] != 'C') return 11;
  if (fgetc(rf) != EOF || !feof(rf)) return 12;
  if (ungetc('Z', rf) != 'Z' || feof(rf)) return 13;
  if (fgetc(rf) != 'Z') return 14;
  if (ungetc(EOF, rf) != EOF) return 15;
  if (fclose(rf) != 0) return 16;

  FILE *rf2 = fopen("tmp.txt", "r");
  if (!rf2) return 17;
  if (fgetc(rf2) != 'A') return 18;
  if (ungetc('L', rf2) != 'L') return 19;
  char line[4];
  if (fgets(line, sizeof(line), rf2) != line) return 20;
  if (line[0] != 'L' || line[1] != 'B' || line[2] != '\n' || line[3] != 0) return 21;
  if (ungetc('M', rf2) != 'M') return 22;
  if (ungetc('N', rf2) != EOF) return 23;
  if (fgetc(rf2) != 'M') return 24;
  if (fclose(rf2) != 0) return 25;

  FILE *sw = fopen("tmp.txt", "w");
  if (!sw) return 26;
  if (fputc('1', sw) != '1' || fputc('2', sw) != '2' ||
      fputc(' ', sw) != ' ' || fputc('3', sw) != '3' ||
      fputc('4', sw) != '4') return 27;
  if (fclose(sw) != 0) return 28;

  FILE *sf = fopen("tmp.txt", "r");
  if (!sf) return 29;
  if (fgetc(sf) != '1') return 30;
  if (ungetc('9', sf) != '9') return 31;
  int a = 0;
  int b = 0;
  int n = 0;
  if (fscanf(sf, "%d %d%n", &a, &b, &n) != 2) return 32;
  if (a != 92 || b != 34 || n != 5) return 33;
  if (fgetc(sf) != EOF || !feof(sf)) return 34;
  if (fclose(sf) != 0) return 35;

  FILE *sf2 = fopen("tmp.txt", "r");
  if (!sf2) return 36;
  if (fgetc(sf2) != '1') return 37;
  if (ungetc('Q', sf2) != 'Q') return 38;
  int bad = 77;
  if (fscanf(sf2, "%d", &bad) != 0 || bad != 77) return 39;
  if (fgetc(sf2) != 'Q') return 40;
  if (fgetc(sf2) != '2') return 41;
  if (fclose(sf2) != 0) return 49;

  FILE *sf3 = fopen("tmp.txt", "r");
  if (!sf3) return 50;
  while (fgetc(sf3) != EOF) {}
  if (!feof(sf3)) return 51;
  if (ungetc('7', sf3) != '7' || feof(sf3)) return 52;
  int eof_value = 0;
  if (fscanf(sf3, "%d", &eof_value) != 1 || eof_value != 7) return 53;
  if (fgetc(sf3) != EOF || !feof(sf3)) return 54;
  if (fclose(sf3) != 0) return 55;
  return 42;
}
SRC

cat > "$out_dir/setvbuf_state.c" <<'SRC'
typedef void FILE;
#define BUFSIZ 8192
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, unsigned long size);
int main(void) {
  char buf[BUFSIZ];
  if (setvbuf((FILE *)1, 0, _IONBF, 0) != 0) return 1;
  if (setvbuf((FILE *)2, buf, _IOLBF, sizeof(buf)) != 0) return 2;
  if (setvbuf((FILE *)1, buf, 99, sizeof(buf)) == 0) return 3;
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 4;
  if (setvbuf(wf, buf, _IOFBF, sizeof(buf)) != 0) return 5;
  setbuf(wf, 0);
  if (fputc('B', wf) != 'B' || fclose(wf) != 0) return 6;
  FILE *rf = fopen("tmp.txt", "r");
  if (!rf) return 7;
  setbuf(rf, buf);
  if (fgetc(rf) != 'B') return 8;
  if (fclose(rf) != 0) return 9;
  return 42;
}
SRC

cat > "$out_dir/fpos_state.c" <<'SRC'
typedef void FILE;
typedef long fpos_t;
#define EOF (-1)
FILE *fopen(char *path, char *mode);
int fclose(FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
int ungetc(int c, FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, fpos_t *pos);
int main(void) {
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 1;
  if (fputc('A', wf) != 'A' || fputc('B', wf) != 'B' ||
      fputc('C', wf) != 'C' || fclose(wf) != 0) return 2;

  FILE *rf = fopen("tmp.txt", "r");
  if (!rf) return 3;
  if (fgetc(rf) != 'A') return 4;
  fpos_t pos = -1;
  if (fgetpos(rf, &pos) != 0 || pos != 1) return 5;
  if (fgetc(rf) != 'B') return 6;
  if (fsetpos(rf, &pos) != 0) return 7;
  if (fgetc(rf) != 'B') return 8;
  if (ungetc('X', rf) != 'X') return 9;
  if (fsetpos(rf, &pos) != 0) return 10;
  if (fgetc(rf) != 'B') return 11;
  if (fgetpos(rf, 0) == 0) return 12;
  if (fsetpos(rf, 0) == 0) return 13;
  if (fclose(rf) != 0) return 14;
  return 42;
}
SRC

cat > "$out_dir/update_file_state.c" <<'SRC'
typedef void FILE;
#define EOF (-1)
#define SEEK_SET 0
#define L_tmpnam 32
#define TMP_MAX 10000
FILE *fopen(char *path, char *mode);
FILE *tmpfile(void);
char *tmpnam(char *s);
int fclose(FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
int same_text(char *a, char *b) {
  int i = 0;
  while (a[i] && b[i] && a[i] == b[i]) i++;
  return a[i] == b[i];
}
int main(void) {
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 1;
  if (fputc('A', wf) != 'A' || fputc('B', wf) != 'B' ||
      fputc('C', wf) != 'C' || fclose(wf) != 0) return 2;

  FILE *rp = fopen("tmp.txt", "r+");
  if (!rp) return 3;
  if (fgetc(rp) != 'A') return 4;
  if (fseek(rp, 1, SEEK_SET) != 0) return 5;
  if (fputc('Z', rp) != 'Z') return 6;
  if (fseek(rp, 0, SEEK_SET) != 0) return 7;
  if (fgetc(rp) != 'A' || fgetc(rp) != 'Z' ||
      fgetc(rp) != 'C' || fgetc(rp) != EOF) return 8;
  if (fclose(rp) != 0) return 9;

  FILE *wp = fopen("tmp.txt", "w+");
  if (!wp) return 10;
  if (fputc('1', wp) != '1' || fputc('2', wp) != '2') return 11;
  if (fseek(wp, 0, SEEK_SET) != 0) return 12;
  if (fgetc(wp) != '1') return 13;
  if (fseek(wp, 1, SEEK_SET) != 0) return 14;
  if (fputc('9', wp) != '9') return 15;
  if (fseek(wp, 0, SEEK_SET) != 0) return 16;
  if (fgetc(wp) != '1' || fgetc(wp) != '9' || fgetc(wp) != EOF) return 17;
  if (fclose(wp) != 0) return 18;

  FILE *tp = tmpfile();
  if (!tp) return 19;
  if (fputc('T', tp) != 'T') return 20;
  if (fseek(tp, 0, SEEK_SET) != 0) return 21;
  if (fgetc(tp) != 'T' || fgetc(tp) != EOF) return 22;
  if (fclose(tp) != 0) return 23;

  char name1[L_tmpnam];
  char name2[L_tmpnam];
  if (TMP_MAX < 2) return 24;
  if (tmpnam(name1) != name1 || name1[0] == 0) return 25;
  char *static_name = tmpnam(0);
  if (!static_name || static_name[0] == 0) return 26;
  if (tmpnam(name2) != name2 || name2[0] == 0) return 27;
  if (same_text(name1, name2)) return 28;
  FILE *tn = fopen(name1, "w+");
  if (!tn) return 29;
  if (fputc('N', tn) != 'N') return 30;
  if (fseek(tn, 0, SEEK_SET) != 0) return 31;
  if (fgetc(tn) != 'N') return 32;
  if (fclose(tn) != 0) return 33;
  return 42;
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
struct timespec {
  time_t tv_sec;
  long tv_nsec;
};
#define TIME_UTC 1
time_t time(time_t *tloc);
double difftime(time_t end, time_t beginning);
int timespec_get(struct timespec *ts, int base);
struct tm *gmtime(time_t *timer);
struct tm *localtime(time_t *timer);
time_t mktime(struct tm *timeptr);
char *asctime(struct tm *timeptr);
char *ctime(time_t *timer);
unsigned long strftime(char *s, unsigned long maxsize, char *format, struct tm *timeptr);
unsigned long wcsftime(int *s, unsigned long maxsize, int *format, struct tm *timeptr);
int same_text(char *a, char *b) {
  while (*a || *b) {
    if (*a != *b) return 0;
    a++;
    b++;
  }
  return 1;
}
int same_wide(int *a, int *b) {
  while (*a || *b) {
    if (*a != *b) return 0;
    a++;
    b++;
  }
  return 1;
}
int main(void) {
  time_t stored = -1;
  time_t now = time(&stored);
  struct tm *tm = localtime(&stored);
  time_t sample = 90061;
  struct tm *gtm;
  char stamp[64];
  int wstamp[64];
  int wfmt[32];
  int wexpect[64];
  unsigned long stamp_len;
  unsigned long wstamp_len;
  struct timespec ts = {-1, -1};
  struct tm mk = {0};
  time_t made;
  if (!(now == 0 && stored == 0 && tm != 0 &&
        tm->tm_sec == 0 && tm->tm_min == 0 && tm->tm_hour == 0 &&
        tm->tm_mday == 1 && tm->tm_mon == 0 && tm->tm_year == 70 &&
        tm->tm_wday == 4 && tm->tm_yday == 0 && tm->tm_isdst == 0 &&
        (int)difftime(10, 3) == 7)) return 1;
  if (timespec_get(&ts, TIME_UTC) != TIME_UTC || ts.tv_sec != 0 || ts.tv_nsec != 0) return 9;
  if (timespec_get(&ts, 99) != 0) return 10;
  gtm = gmtime(&sample);
  if (!(gtm != 0 && gtm->tm_sec == 1 && gtm->tm_min == 1 && gtm->tm_hour == 1 &&
        gtm->tm_mday == 2 && gtm->tm_mon == 0 && gtm->tm_year == 70 &&
        gtm->tm_wday == 5 && gtm->tm_yday == 1 && gtm->tm_isdst == 0)) return 2;
  if (!same_text(asctime(gtm), "Fri Jan  2 01:01:01 1970\n")) return 3;
  if (!same_text(ctime(&sample), "Fri Jan  2 01:01:01 1970\n")) return 4;
  stamp_len = strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S %a %b %j %%", gtm);
  if (stamp_len != 33 || !same_text(stamp, "1970-01-02 01:01:01 Fri Jan 002 %")) return 5;
  if (strftime(stamp, 8, "%Y-%m-%d", gtm) != 0) return 6;
  wfmt[0] = '%'; wfmt[1] = 'F'; wfmt[2] = ' '; wfmt[3] = '%'; wfmt[4] = 'T';
  wfmt[5] = ' '; wfmt[6] = '%'; wfmt[7] = 'a'; wfmt[8] = 0;
  wexpect[0] = '1'; wexpect[1] = '9'; wexpect[2] = '7'; wexpect[3] = '0';
  wexpect[4] = '-'; wexpect[5] = '0'; wexpect[6] = '1'; wexpect[7] = '-';
  wexpect[8] = '0'; wexpect[9] = '2'; wexpect[10] = ' '; wexpect[11] = '0';
  wexpect[12] = '1'; wexpect[13] = ':'; wexpect[14] = '0'; wexpect[15] = '1';
  wexpect[16] = ':'; wexpect[17] = '0'; wexpect[18] = '1'; wexpect[19] = ' ';
  wexpect[20] = 'F'; wexpect[21] = 'r'; wexpect[22] = 'i'; wexpect[23] = 0;
  wstamp_len = wcsftime(wstamp, 64, wfmt, gtm);
  if (wstamp_len != 23 || !same_wide(wstamp, wexpect)) return 7;
  if (wcsftime(wstamp, 8, wfmt, gtm) != 0) return 8;
  mk.tm_sec = 0;
  mk.tm_min = 0;
  mk.tm_hour = 0;
  mk.tm_mday = 3;
  mk.tm_mon = 0;
  mk.tm_year = 70;
  mk.tm_wday = 0;
  mk.tm_yday = 0;
  mk.tm_isdst = -1;
  made = mktime(&mk);
  if (made != 172800 || mk.tm_wday != 6 || mk.tm_yday != 2 || mk.tm_isdst != 0) return 11;
  return 42;
}
SRC

cat > "$out_dir/wide_strto_state.c" <<'SRC'
long wcstol(int *s, int **endptr, int base);
unsigned long wcstoul(int *s, int **endptr, int base);
long long wcstoll(int *s, int **endptr, int base);
unsigned long long wcstoull(int *s, int **endptr, int base);
float wcstof(int *s, int **endptr);
double wcstod(int *s, int **endptr);
long double wcstold(int *s, int **endptr);
int *__error(void);
int main(void) {
  int hex[] = {'0', 'x', '1', '0', '!', 0};
  int oct[] = {'0', '1', '0', '!', 0};
  int none[] = {' ', '+', 'x', 0};
  int dot[] = {'.', '2', '5', '!', 0};
  int expf[] = {'-', '1', '.', '2', '5', 'e', '2', '!', 0};
  int hexfloat[] = {'0', 'x', '1', '.', '8', 'p', '+', '3', '!', 0};
  int llhex[] = {'-', '8', '0', '0', '0', '0', '0', '0', '0', '!', 0};
  int ullhex[] = {'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', '!', 0};
  int dinf[] = {'i', 'n', 'f', '!', 0};
  int ov[] = {'9', '2', '2', '3', '3', '7', '2', '0', '3', '6', '8', '5', '4', '7', '7', '5', '8', '0', '8', '!', 0};
  int only_dot[] = {'.', 'x', 0};
  int *end = 0;
  int *errp = __error();
  long h = wcstol(hex, &end, 0);
  if (h != 16) return 31;
  if (*end != '!') return 32;
  int ok_hex = 1;
  long o = wcstol(oct, &end, 0);
  int ok_oct = o == 8 && *end == '!';
  unsigned long u = wcstoul(oct, &end, 0);
  int ok_u = u == 8 && *end == '!';
  long no = wcstol(none, &end, 0);
  int ok_no = no == 0 && end == none;
  int b36s[] = {'z', '!', 0};
  long b36 = wcstol(b36s, &end, 36);
  int ok_b36 = b36 == 35 && *end == '!';
  long bad = wcstol(hex, &end, 37);
  int ok_bad = bad == 0 && end == hex;
  double d = wcstod(dot, &end);
  int ok_dot = d == 0.25 && *end == '!';
  float f = wcstof(expf, &end);
  int ok_float = (int)f == -125 && *end == '!';
  long double ld = wcstold(hexfloat, &end);
  int ok_ld = (int)ld == 12 && *end == '!';
  long long ll = wcstoll(llhex, &end, 16);
  int ok_ll = ll == -2147483648LL && *end == '!';
  unsigned long long ull = wcstoull(ullhex, &end, 16);
  int ok_ull = ull == 4294967295ULL && *end == '!';
  *errp = 0;
  long long llov = wcstoll(ov, &end, 10);
  int ok_ov = llov == 9223372036854775807LL && *end == '!' && *errp == 34;
  *errp = 0;
  double inf = wcstod(dinf, &end);
  int ok_inf = inf > 1e300 && *end == '!' && *errp == 0;
  double nd = wcstod(none, &end);
  int ok_nd = nd == 0.0 && end == none;
  double od = wcstod(only_dot, &end);
  int ok_od = od == 0.0 && end == only_dot;
  if (!ok_hex) return 1;
  if (!ok_oct) return 2;
  if (!ok_u) return 3;
  if (!ok_no) return 4;
  if (!ok_b36) return 5;
  if (!ok_bad) return 6;
  if (!ok_dot) return 7;
  if (!ok_float) return 8;
  if (!ok_ld) return 9;
  if (!ok_ll) return 10;
  if (!ok_ull) return 11;
  if (!ok_ov) return 12;
  if (!ok_inf) return 13;
  if (!ok_nd) return 14;
  if (!ok_od) return 15;
  return 42;
}
SRC

cat > "$out_dir/utf8_wide_state.c" <<'SRC'
int mblen(char *s, unsigned long n);
int mbtowc(int *pwc, char *s, unsigned long n);
int wctomb(char *s, int wc);
unsigned long mbstowcs(int *dst, char *src, unsigned long n);
unsigned long wcstombs(char *dst, int *src, unsigned long n);
unsigned long mbrtowc(int *pwc, char *s, unsigned long n, void *ps);
unsigned long mbrlen(char *s, unsigned long n, void *ps);
int mbsinit(void *ps);
unsigned long wcrtomb(char *s, int wc, void *ps);
unsigned long mbrtoc16(unsigned short *pc16, char *s, unsigned long n, void *ps);
unsigned long c16rtomb(char *s, unsigned short c16, void *ps);
unsigned long mbrtoc32(unsigned int *pc32, char *s, unsigned long n, void *ps);
unsigned long c32rtomb(char *s, unsigned int c32, void *ps);
unsigned long mbsrtowcs(int *dst, char **src, unsigned long len, void *ps);
unsigned long wcsrtombs(char *dst, int **src, unsigned long len, void *ps);
int main(void) {
  void *nullv = 0;
  char jp[9];
  jp[0] = 'A';
  jp[1] = (char)0xe3;
  jp[2] = (char)0x81;
  jp[3] = (char)0x82;
  jp[4] = (char)0xf0;
  jp[5] = (char)0x9f;
  jp[6] = (char)0x98;
  jp[7] = (char)0x80;
  jp[8] = 0;
  int wc = 0;
  char out[12];
  int wide[4];
  char *srcp = jp;
  int *widep;
  unsigned short c16 = 0;
  unsigned int c32 = 0;
  int ok_mbr = mbrtowc(&wc, jp + 1, 4, nullv) == 3 && wc == 0x3042 &&
               mbrtowc(&wc, jp + 4, 4, nullv) == 4 && wc == 0x1f600 &&
               mbrtowc(&wc, jp + 1, 2, nullv) == (unsigned long)-2;
  int ok_mbrlen = mbrlen(jp + 1, 4, nullv) == 3 &&
                  mbrlen(jp + 4, 4, nullv) == 4 &&
                  mbrlen(jp + 1, 2, nullv) == (unsigned long)-2 &&
                  mbrlen(0, 0, nullv) == 0 && mbsinit(nullv) == 1;
  int wr = wcrtomb(out, 0x3042, nullv);
  int ok_wcr = wr == 3 && (unsigned char)out[0] == 0xe3 &&
               (unsigned char)out[1] == 0x81 && (unsigned char)out[2] == 0x82;
  int ok16 = mbrtoc16(&c16, jp + 1, 4, nullv) == 3 && c16 == 0x3042 &&
             c16rtomb(out, c16, nullv) == 3 && (unsigned char)out[0] == 0xe3;
  int ok32 = mbrtoc32(&c32, jp + 4, 4, nullv) == 4 && c32 == 0x1f600 &&
             c32rtomb(out, c32, nullv) == 4 && (unsigned char)out[0] == 0xf0;
  unsigned long wn = mbsrtowcs(wide, &srcp, 4, nullv);
  widep = wide;
  unsigned long bn = wcsrtombs(out, &widep, sizeof(out), nullv);
  int mbwc = 0;
  char mbout[12];
  int mbwide[4];
  int ok_legacy = mblen(jp + 1, 4) == 3 && mblen(jp + 1, 2) == -1 &&
                  mbtowc(&mbwc, jp + 1, 4) == 3 && mbwc == 0x3042 &&
                  wctomb(mbout, 0x3042) == 3 &&
                  (unsigned char)mbout[0] == 0xe3 &&
                  mbstowcs(mbwide, jp, 4) == 3 && mbwide[0] == 'A' &&
                  mbwide[1] == 0x3042 && mbwide[2] == 0x1f600 &&
                  wcstombs(mbout, mbwide, sizeof(mbout)) == 8 &&
                  mbout[0] == 'A' && (unsigned char)mbout[1] == 0xe3;
  int ok_round = wn == 3 && srcp == 0 && wide[0] == 'A' &&
                 wide[1] == 0x3042 && wide[2] == 0x1f600 && wide[3] == 0 &&
                 bn == 8 && widep == 0 && out[0] == 'A' &&
                 (unsigned char)out[1] == 0xe3 && (unsigned char)out[2] == 0x81 &&
                 (unsigned char)out[3] == 0x82 && (unsigned char)out[4] == 0xf0 &&
                 (unsigned char)out[5] == 0x9f && (unsigned char)out[6] == 0x98 &&
                 (unsigned char)out[7] == 0x80 && out[8] == 0;
  return ok_mbr && ok_mbrlen && ok_wcr && ok16 && ok32 && ok_round && ok_legacy ? 42 : 1;
}
SRC

cat > "$out_dir/wide_io_state.c" <<'SRC'
#include <stdio.h>
#include <wchar.h>
int main(void) {
  FILE *fp;
  wchar_t line[8];
  wchar_t out[4];
  if (fwide(0, 0) != 0 || fwide(0, 1) != 1 || fwide(0, -1) != -1) return 1;
  fp = fopen("wide.txt", "w+");
  if (!fp) return 2;
  out[0] = 'O';
  out[1] = 0x3042;
  out[2] = '\n';
  out[3] = 0;
  if (fputwc('A', fp) != 'A') return 3;
  if (putwc(0x3042, fp) != 0x3042) return 4;
  if (fputwc('\n', fp) != '\n') return 5;
  if (fputws(out, fp) != 3) return 6;
  rewind(fp);
  if (fgetwc(fp) != 'A') return 7;
  if (ungetwc('Z', fp) != 'Z') return 8;
  if (getwc(fp) != 'Z') return 9;
  if (fgetwc(fp) != 0x3042) return 10;
  if (fgetwc(fp) != '\n') return 11;
  if (fgetws(line, 8, fp) != line) return 12;
  if (line[0] != 'O' || line[1] != 0x3042 || line[2] != '\n' || line[3] != 0) return 13;
  if (fgetwc(fp) != WEOF) return 14;
  if (ungetwc(0x3042, fp) != WEOF) return 15;
  if (fclose(fp) != 0) return 16;
  if (putwchar('Q') != 'Q') return 17;
  return 42;
}
SRC

cat > "$out_dir/alloc_state.c" <<'SRC'
void *malloc(long size);
void *calloc(long nmemb, long size);
void *realloc(void *ptr, long size);
void *aligned_alloc(long alignment, long size);
int main(void) {
  char *p = malloc(4);
  char *q;
  char *r;
  char *a16;
  char *a32;
  void *bad_malloc = malloc(-1);
  void *bad_malloc_large = malloc(60L * 1024L * 1024L);
  void *bad_calloc_neg = calloc(-1, 4);
  void *bad_calloc_overflow = calloc(1L << 62, 16);
  void *bad_align_nonpow2 = aligned_alloc(12, 24);
  void *bad_align_size = aligned_alloc(16, 24);
  void *bad_align_neg = aligned_alloc(16, -16);
  void *zero_calloc = calloc(0, 99);
  if (!p) return 1;
  p[0] = 'A';
  p[1] = 'B';
  p[2] = 'C';
  p[3] = 0;
  q = realloc(p, 8);
  if (!q) return 2;
  r = realloc(q, 2);
  if (!r) return 3;
  a16 = aligned_alloc(16, 32);
  a32 = aligned_alloc(32, 0);
  if (!a16 || !a32) return 4;
  a16[0] = 'Z';
  a16[31] = 'Q';
  return bad_malloc == 0 && bad_malloc_large == 0 &&
         bad_calloc_neg == 0 && bad_calloc_overflow == 0 &&
         bad_align_nonpow2 == 0 && bad_align_size == 0 && bad_align_neg == 0 &&
         ((long)a16 % 16) == 0 && ((long)a32 % 32) == 0 &&
         a16[0] == 'Z' && a16[31] == 'Q' &&
         zero_calloc != 0 && q != p && q[0] == 'A' && q[1] == 'B' &&
         q[2] == 'C' && r != q && r[0] == 'A' && r[1] == 'B' &&
         realloc(r, -1) == 0 && realloc(r, 0) == 0 ? 42 : 1;
}
SRC

cat > "$out_dir/qsort_size_state.c" <<'SRC'
void qsort(void *base, long nmemb, long size, int (*compar)(void *, void *));
void *bsearch(void *key, void *base, long nmemb, long size, int (*compar)(void *, void *));
int calls;
int cmp_int(void *ap, void *bp) {
  int *a = (int *)ap;
  int *b = (int *)bp;
  calls = calls + 1;
  return *a - *b;
}
int main(void) {
  int nums[3];
  int key = 2;
  int *found;
  int pair[2];
  void *bad_search;
  nums[0] = 3;
  nums[1] = 1;
  nums[2] = 2;
  qsort(nums, 3, sizeof(int), cmp_int);
  found = bsearch(&key, nums, 3, sizeof(int), cmp_int);
  if (!(nums[0] == 1 && nums[1] == 2 && nums[2] == 3 && found == nums + 1)) return 1;
  calls = 0;
  pair[0] = 9;
  pair[1] = 8;
  qsort(pair, 2, 1L << 62, cmp_int);
  bad_search = bsearch(&key, pair, 2, 1L << 62, cmp_int);
  if (!(calls == 0 && pair[0] == 9 && pair[1] == 8 && bad_search == 0)) return 2;
  qsort(pair, 1L << 62, 8, cmp_int);
  bad_search = bsearch(&key, pair, 1L << 62, 8, cmp_int);
  return calls == 0 && pair[0] == 9 && pair[1] == 8 && bad_search == 0 ? 42 : 1;
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
void *aligned_alloc(long alignment, long size);
void free(void *p);
int atexit(void *func);
int at_quick_exit(void *func);
double atof(char *s);
long atol(char *s);
long long atoll(char *s);
long strtol(char *s, char **endptr, int base);
unsigned long strtoul(char *s, char **endptr, int base);
long long strtoll(char *s, char **endptr, int base);
unsigned long long strtoull(char *s, char **endptr, int base);
float strtof(char *s, char **endptr);
double strtod(char *s, char **endptr);
long double strtold(char *s, char **endptr);
long strtoimax(char *s, char **endptr, int base);
unsigned long strtoumax(char *s, char **endptr, int base);
int rand(void);
void srand(int seed);
long labs(long n);
long long llabs(long long n);
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;
typedef struct { long long quot; long long rem; } imaxdiv_t;
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);
imaxdiv_t imaxdiv(long long numer, long long denom);
void qsort(void *base, long nmemb, long size, int (*compar)(void *, void *));
void *bsearch(void *key, void *base, long nmemb, long size, int (*compar)(void *, void *));
void exit(int status);
void quick_exit(int status);
void _Exit(int status);
void abort(void);
int *__error(void);
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
double nearbyint(double x);
float nearbyintf(float x);
long double nearbyintl(long double x);
double rint(double x);
float rintf(float x);
long double rintl(long double x);
long lrint(double x);
long lrintf(float x);
long lrintl(long double x);
long long llrint(double x);
long long llrintf(float x);
long long llrintl(long double x);
long lround(double x);
long lroundf(float x);
long lroundl(long double x);
long long llround(double x);
long long llroundf(float x);
long long llroundl(long double x);
double fmod(double x, double y);
float fmodf(float x, float y);
long double fmodl(long double x, long double y);
double remainder(double x, double y);
float remainderf(float x, float y);
long double remainderl(long double x, long double y);
double remquo(double x, double y, int *quo);
float remquof(float x, float y, int *quo);
long double remquol(long double x, long double y, int *quo);
double fdim(double x, double y);
float fdimf(float x, float y);
long double fdiml(long double x, long double y);
double fma(double x, double y, double z);
float fmaf(float x, float y, float z);
long double fmal(long double x, long double y, long double z);
double frexp(double x, int *exp);
float frexpf(float x, int *exp);
long double frexpl(long double x, int *exp);
double ldexp(double x, int exp);
float ldexpf(float x, int exp);
long double ldexpl(long double x, int exp);
double scalbn(double x, int exp);
float scalbnf(float x, int exp);
long double scalbnl(long double x, int exp);
double scalbln(double x, long exp);
float scalblnf(float x, long exp);
long double scalblnl(long double x, long exp);
int ilogb(double x);
int ilogbf(float x);
int ilogbl(long double x);
double logb(double x);
float logbf(float x);
long double logbl(long double x);
double modf(double x, double *iptr);
float modff(float x, float *iptr);
long double modfl(long double x, long double *iptr);
double copysign(double x, double y);
float copysignf(float x, float y);
long double copysignl(long double x, long double y);
double nan(char *tagp);
float nanf(char *tagp);
long double nanl(char *tagp);
double cbrt(double x);
double exp(double x);
float expf(float x);
long double expl(long double x);
double exp2(double x);
float exp2f(float x);
long double exp2l(long double x);
double expm1(double x);
float expm1f(float x);
long double expm1l(long double x);
double erf(double x);
float erff(float x);
long double erfl(long double x);
double erfc(double x);
float erfcf(float x);
long double erfcl(long double x);
double log(double x);
float logf(float x);
long double logl(long double x);
double log1p(double x);
float log1pf(float x);
long double log1pl(long double x);
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
float sinhf(float x);
long double sinhl(long double x);
double cosh(double x);
float coshf(float x);
long double coshl(long double x);
double tanh(double x);
float tanhf(float x);
long double tanhl(long double x);
double asinh(double x);
float asinhf(float x);
long double asinhl(long double x);
double acosh(double x);
float acoshf(float x);
long double acoshl(long double x);
double atanh(double x);
float atanhf(float x);
long double atanhl(long double x);
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
int fpclassify(double x);
int isfinite(double x);
int isinf(double x);
int isnan(double x);
int isnormal(double x);
int signbit(double x);
int isgreater(double x, double y);
int isgreaterequal(double x, double y);
int isless(double x, double y);
int islessequal(double x, double y);
int islessgreater(double x, double y);
int isunordered(double x, double y);
int atoi(char *s);
char *strcpy(char *dst, char *src);
char *strncpy(char *dst, char *src, unsigned long n);
char *strcat(char *dst, char *src);
char *strncat(char *dst, char *src, unsigned long n);
int strncmp(char *a, char *b, unsigned long n);
int strcoll(char *a, char *b);
unsigned long strxfrm(char *dst, char *src, unsigned long n);
int memcmp(void *a, void *b, unsigned long n);
void *memmove(void *dst, void *src, unsigned long n);
void *memchr(void *s, int ch, unsigned long n);
char *strchr(char *s, int ch);
char *strrchr(char *s, int ch);
char *strstr(char *haystack, char *needle);
unsigned long strspn(char *s, char *accept);
unsigned long strcspn(char *s, char *reject);
char *strpbrk(char *s, char *accept);
char *strtok(char *str, char *delim);
char *strerror(int errnum);
long wcslen(int *s);
int *wcscpy(int *dst, int *src);
int *wcsncpy(int *dst, int *src, unsigned long n);
int *wcscat(int *dst, int *src);
int *wcsncat(int *dst, int *src, unsigned long n);
int wcscmp(int *a, int *b);
int wcsncmp(int *a, int *b, unsigned long n);
int wcscoll(int *a, int *b);
unsigned long wcsxfrm(int *dst, int *src, unsigned long n);
int *wcschr(int *s, int ch);
int *wcsrchr(int *s, int ch);
int *wcsstr(int *s, int *sub);
unsigned long wcsspn(int *s, int *accept);
unsigned long wcscspn(int *s, int *reject);
int *wcspbrk(int *s, int *accept);
int *wcstok(int *s, int *delim, int **ptr);
int *wmemcpy(int *dst, int *src, unsigned long n);
int *wmemmove(int *dst, int *src, unsigned long n);
int *wmemset(int *s, int ch, unsigned long n);
int wmemcmp(int *a, int *b, unsigned long n);
int *wmemchr(int *s, int ch, unsigned long n);
long wcstol(int *s, int **endptr, int base);
unsigned long wcstoul(int *s, int **endptr, int base);
long long wcstoll(int *s, int **endptr, int base);
unsigned long long wcstoull(int *s, int **endptr, int base);
float wcstof(int *s, int **endptr);
double wcstod(int *s, int **endptr);
long double wcstold(int *s, int **endptr);
int mblen(char *s, unsigned long n);
int mbtowc(int *pwc, char *s, unsigned long n);
int wctomb(char *s, int wc);
unsigned long mbstowcs(int *dst, char *src, unsigned long n);
unsigned long wcstombs(char *dst, int *src, unsigned long n);
unsigned long mbrtowc(int *pwc, char *s, unsigned long n, void *ps);
unsigned long mbrlen(char *s, unsigned long n, void *ps);
int mbsinit(void *ps);
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
typedef void FILE;
int fgetwc(FILE *stream);
int getwc(FILE *stream);
int getwchar(void);
int fputwc(int wc, FILE *stream);
int putwc(int wc, FILE *stream);
int putwchar(int wc);
int ungetwc(int wc, FILE *stream);
int *fgetws(int *s, int n, FILE *stream);
int fputws(int *s, FILE *stream);
int fwide(FILE *stream, int mode);
int sscanf(char *s, char *fmt, ...);
typedef void (*sig_handler_t)(int);
sig_handler_t signal(int sig, sig_handler_t handler);
int raise(int sig);
int wctype(char *property);
int iswctype(int wc, int desc);
int wctrans(char *property);
int towctrans(int wc, int desc);
int putchar(int c);
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
struct timespec {
  long tv_sec;
  long tv_nsec;
};
#define TIME_UTC 1
typedef long jmp_buf[48];
FILE *fopen(char *path, char *mode);
FILE *freopen(char *path, char *mode, FILE *stream);
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
int vprintf(char *fmt, va_list ap);
int vfprintf(FILE *stream, char *fmt, va_list ap);
int vsprintf(char *buf, char *fmt, va_list ap);
int vsnprintf(char *buf, unsigned long size, char *fmt, va_list ap);
int scanf(char *fmt, ...);
int fscanf(FILE *stream, char *fmt, ...);
int puts(char *s);
int fputs(char *s, FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int fflush(FILE *stream);
void perror(char *s);
int getchar(void);
int getrusage(int who, struct rusage *usage);
int timespec_get(struct timespec *ts, int base);
struct tm *gmtime(long *timer);
struct tm *localtime(long *timer);
long mktime(struct tm *timeptr);
char *asctime(struct tm *timeptr);
char *ctime(long *timer);
unsigned long strftime(char *s, unsigned long maxsize, char *format, struct tm *timeptr);
unsigned long wcsftime(int *s, unsigned long maxsize, int *format, struct tm *timeptr);
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
int call_vsprintf(char *buf, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsprintf(buf, fmt, ap);
  va_end(ap);
  return n;
}
int call_vprintf(char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vprintf(fmt, ap);
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
int math_decomp_check(void) {
  double math_zero = 0.0;
  double math_nzero = -math_zero;
  int dexp = 0;
  int fexp = 0;
  int lexp = 0;
  int dquo = 0;
  int fquo = 0;
  int lquo = 0;
  int dquo_bits = 0;
  int dquo_neg_bits = 0;
  double dint = 0.0;
  float fint = 0.0f;
  long double lint = 0.0L;
  double dfrac = modf(-3.75, &dint);
  float ffrac = modff(2.25f, &fint);
  long double lfrac = modfl(5.5L, &lint);
  double dmant = frexp(-8.0, &dexp);
  float fmant = frexpf(4.0f, &fexp);
  long double lmant = frexpl(16.0L, &lexp);
  double dcopysign = copysign(2.0, math_nzero);
  float fcopysign = copysignf(2.0f, -0.0f);
  long double lcopysign = copysignl(2.0L, -0.0L);
  double dnan_api = nan("");
  float fnan_api = nanf("");
  long double lnan_api = nanl("");
  return (int)(dmant * 1000.0) == -500 && dexp == 4 &&
         (int)(fmant * 1000.0f) == 500 && fexp == 3 &&
         (int)(lmant * 1000.0L) == 500 && lexp == 5 &&
         (int)(ldexp(0.75, 3) * 1000.0) == 6000 &&
         (int)(ldexpf(0.5f, 4) * 1000.0f) == 8000 &&
         (int)(ldexpl(0.25L, 5) * 1000.0L) == 8000 &&
         (int)(scalbn(0.75, 4) * 1000.0) == 12000 &&
         (int)(scalbnf(0.5f, 5) * 1000.0f) == 16000 &&
         (int)(scalbnl(0.25L, 6) * 1000.0L) == 16000 &&
         (int)(scalbln(1.5, 3L) * 1000.0) == 12000 &&
         (int)(scalblnf(1.25f, 2L) * 1000.0f) == 5000 &&
         (int)(scalblnl(3.0L, -1L) * 1000.0L) == 1500 &&
         ilogb(8.0) == 3 && ilogbf(0.75f) == -1 && ilogbl(0.25L) == -2 &&
         (int)logb(8.0) == 3 && (int)logbf(0.75f) == -1 &&
         (int)logbl(0.25L) == -2 &&
         (int)(dfrac * 100.0) == -75 && (int)dint == -3 &&
         (int)(ffrac * 100.0f) == 25 && (int)fint == 2 &&
         (int)(lfrac * 100.0L) == 50 && (int)lint == 5 &&
         (int)dcopysign == -2 && signbit(dcopysign) &&
         (int)fcopysign == -2 && signbit(fcopysign) &&
         (int)lcopysign == -2 && signbit(lcopysign) &&
         (int)(fdim(5.5, 2.0) * 1000.0) == 3500 &&
         (int)(fdim(2.0, 5.5) * 1000.0) == 0 &&
         (int)(fdimf(5.5f, 2.0f) * 1000.0f) == 3500 &&
         (int)(fdiml(5.5L, 2.0L) * 1000.0L) == 3500 &&
         (int)(fma(2.0, 3.0, 0.5) * 1000.0) == 6500 &&
         (int)(fmaf(2.0f, 3.0f, 0.5f) * 1000.0f) == 6500 &&
         (int)(fmal(2.0L, 3.0L, 0.5L) * 1000.0L) == 6500 &&
         (int)(remainder(5.5, 2.0) * 1000.0) == -500 &&
         (int)(remainderf(5.5f, 2.0f) * 1000.0f) == -500 &&
         (int)(remainderl(5.5L, 2.0L) * 1000.0L) == -500 &&
         (int)(remquo(5.5, 2.0, &dquo) * 1000.0) == -500 && dquo == 3 &&
         (int)(remquof(5.5f, 2.0f, &fquo) * 1000.0f) == -500 && fquo == 3 &&
         (int)(remquol(5.5L, 2.0L, &lquo) * 1000.0L) == -500 && lquo == 3 &&
         (int)(remquo(19.5, 2.0, &dquo_bits) * 1000.0) == -500 && dquo_bits == 2 &&
         (int)(remquo(-19.5, 2.0, &dquo_neg_bits) * 1000.0) == 500 && dquo_neg_bits == -2 &&
         isnan(dnan_api) && isnan(fnan_api) && isnan(lnan_api);
}
int math_exp_log_ext_check(void) {
  int exp2v = (int)(exp2(3.0) * 1000.0);
  int exp2fv = (int)(exp2f(3.0f) * 1000.0f);
  int exp2lv = (int)(exp2l(3.0L) * 1000.0L);
  int expm1v = (int)(expm1(1.0) * 1000.0);
  int expm1fv = (int)(expm1f(1.0f) * 1000.0f);
  int expm1lv = (int)(expm1l(1.0L) * 1000.0L);
  int log1pv = (int)(log1p(1.0) * 1000.0);
  int log1pfv = (int)(log1pf(1.0f) * 1000.0f);
  int log1plv = (int)(log1pl(1.0L) * 1000.0L);
  int sinhv = (int)(sinh(1.0) * 1000.0);
  int sinhfv = (int)(sinhf(1.0f) * 1000.0f);
  int sinhlv = (int)(sinhl(1.0L) * 1000.0L);
  int coshv = (int)(cosh(1.0) * 1000.0);
  int coshfv = (int)(coshf(1.0f) * 1000.0f);
  int coshlv = (int)(coshl(1.0L) * 1000.0L);
  int tanhv = (int)(tanh(1.0) * 1000.0);
  int tanhfv = (int)(tanhf(1.0f) * 1000.0f);
  int tanhlv = (int)(tanhl(1.0L) * 1000.0L);
  int asinhv = (int)(asinh(1.0) * 1000.0);
  int asinhfv = (int)(asinhf(1.0f) * 1000.0f);
  int asinhlv = (int)(asinhl(1.0L) * 1000.0L);
  int acoshv = (int)(acosh(2.0) * 1000.0);
  int acoshfv = (int)(acoshf(2.0f) * 1000.0f);
  int acoshlv = (int)(acoshl(2.0L) * 1000.0L);
  int atanhv = (int)(atanh(0.5) * 1000.0);
  int atanhfv = (int)(atanhf(0.5f) * 1000.0f);
  int atanhlv = (int)(atanhl(0.5L) * 1000.0L);
  int erfv = (int)(erf(1.0) * 1000.0);
  int erffv = (int)(erff(1.0f) * 1000.0f);
  int erflv = (int)(erfl(1.0L) * 1000.0L);
  int erfcv = (int)(erfc(1.0) * 1000.0);
  int erfcfv = (int)(erfcf(1.0f) * 1000.0f);
  int erfclv = (int)(erfcl(1.0L) * 1000.0L);
  return exp2v >= 7998 && exp2v <= 8002 &&
         exp2fv >= 7998 && exp2fv <= 8002 &&
         exp2lv >= 7998 && exp2lv <= 8002 &&
         expm1v >= 1716 && expm1v <= 1720 &&
         expm1fv >= 1716 && expm1fv <= 1720 &&
         expm1lv >= 1716 && expm1lv <= 1720 &&
         log1pv >= 691 && log1pv <= 695 &&
         log1pfv >= 691 && log1pfv <= 695 &&
         log1plv >= 691 && log1plv <= 695 &&
         sinhv >= 1174 && sinhv <= 1176 &&
         sinhfv >= 1174 && sinhfv <= 1176 &&
         sinhlv >= 1174 && sinhlv <= 1176 &&
         coshv >= 1542 && coshv <= 1544 &&
         coshfv >= 1542 && coshfv <= 1544 &&
         coshlv >= 1542 && coshlv <= 1544 &&
         tanhv >= 760 && tanhv <= 762 &&
         tanhfv >= 760 && tanhfv <= 762 &&
         tanhlv >= 760 && tanhlv <= 762 &&
         asinhv >= 880 && asinhv <= 882 &&
         asinhfv >= 880 && asinhfv <= 882 &&
         asinhlv >= 880 && asinhlv <= 882 &&
         acoshv >= 1315 && acoshv <= 1317 &&
         acoshfv >= 1315 && acoshfv <= 1317 &&
         acoshlv >= 1315 && acoshlv <= 1317 &&
         atanhv >= 548 && atanhv <= 550 &&
         atanhfv >= 548 && atanhfv <= 550 &&
         atanhlv >= 548 && atanhlv <= 550 &&
         erfv >= 841 && erfv <= 844 &&
         erffv >= 841 && erffv <= 844 &&
         erflv >= 841 && erflv <= 844 &&
         erfcv >= 156 && erfcv <= 158 &&
         erfcfv >= 156 && erfcfv <= 158 &&
         erfclv >= 156 && erfclv <= 158;
}
int math_round_ext_check(void) {
  int ok = 1;
  ok = ok && nearbyint(2.5) == 2.0 && nearbyint(3.5) == 4.0 &&
            nearbyintf(-2.5f) == -2.0f && nearbyintl(-3.5L) == -4.0L;
  ok = ok && lround(2.5) == 3 && lroundf(-2.5f) == -3 &&
            lroundl(3.5L) == 4 && llround(-3.5) == -4 &&
            llroundf(2.5f) == 3 && llroundl(-2.5L) == -3;
  ok = ok && fesetround(0x00400000) == 0 &&
            rint(2.1) == 3.0 && rintf(-2.1f) == -2.0f &&
            lrint(2.1) == 3 && llrintf(-2.1f) == -2;
  ok = ok && fesetround(0x00800000) == 0 &&
            rintl(2.9L) == 2.0L && nearbyint(-2.1) == -3.0 &&
            lrintl(2.9L) == 2 && llrintl(-2.1L) == -3;
  ok = ok && fesetround(0x00C00000) == 0 &&
            rint(2.9) == 2.0 && rint(-2.9) == -2.0 &&
            lrintf(2.9f) == 2 && llrint(-2.9) == -2;
  ok = ok && fesetround(0) == 0 &&
            rint(2.5) == 2.0 && rint(3.5) == 4.0 && lrint(3.5) == 4;
  return ok;
}
int main(void) {
  char a[32];
  char b[32];
  char c[32];
  char d[32];
  char e[32];
  char xfrm[8];
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
  unsigned long xfrm_len = strxfrm(xfrm, "hello", sizeof(xfrm));
  unsigned long xfrm_len_only = strxfrm(xfrm, "abcdef", 0);
  char *p = malloc(8);
  char *q = calloc(4, 1);
  char *r = malloc(2);
  char *aligned16 = aligned_alloc(16, 32);
  char *aligned32 = aligned_alloc(32, 0);
  void *bad_aligned_nonpow2 = aligned_alloc(12, 24);
  void *bad_aligned_size = aligned_alloc(16, 24);
  r[0] = 'A';
  r[1] = 0;
  r = realloc(r, 4);
  if (aligned16) {
    aligned16[0] = 'A';
    aligned16[31] = 'Z';
  }
  char *endp = 0;
  char *uendp = 0;
  char *dendp = 0;
  char *fendp = 0;
  char *ldendp = 0;
  char *imax_endp = 0;
  char *umax_endp = 0;
  char *ll_endp = 0;
  char *ull_endp = 0;
  char *ov_pos_endp = 0;
  char *ov_neg_endp = 0;
  char *uov_endp = 0;
  char *uneg_endp = 0;
  char *bad_base_endp = 0;
  char *dov_endp = 0;
  char *dunder_endp = 0;
  char *dzero_endp = 0;
  char *dinf_endp = 0;
  char *dninf_endp = 0;
  char *dnan_endp = 0;
  char *comma_endp = 0;
  char bad_base_src[] = "123";
  char resolved_path[32];
  char *resolved_pathp = realpath("include", resolved_path);
  char *resolved_nullp = realpath("src", 0);
  long parsed = strtol("  -2a", &endp, 16);
  unsigned long parsed_u = strtoul("  ff!", &uendp, 16);
  double parsed_d = strtod(" -12.5e1!", &dendp);
  float parsed_f = strtof(" .25!", &fendp);
  long double parsed_ld = strtold("0x1.8p+3!", &ldendp);
  double comma_d = strtod("12,5", &comma_endp);
  double parsed_atof = atof(" 3.25x");
  long parsed_imax = strtoimax("  7f!", &imax_endp, 16);
  unsigned long parsed_umax = strtoumax("  377!", &umax_endp, 8);
  long long parsed_ll = strtoll("  -80000000!", &ll_endp, 16);
  unsigned long long parsed_ull = strtoull("  ffffffff!", &ull_endp, 16);
  int *strto_errp = __error();
  *strto_errp = 0;
  double dov = strtod("1e309!", &dov_endp);
  int dov_errno = *strto_errp;
  *strto_errp = 0;
  double dunder = strtod("1e-400!", &dunder_endp);
  int dunder_errno = *strto_errp;
  *strto_errp = 0;
  double dzero = strtod("0e9999!", &dzero_endp);
  int dzero_errno = *strto_errp;
  *strto_errp = 0;
  double dinf = strtod("INF!", &dinf_endp);
  int dinf_errno = *strto_errp;
  *strto_errp = 0;
  float dninf = strtof("-infinity!", &dninf_endp);
  int dninf_errno = *strto_errp;
  *strto_errp = 0;
  long double dnan = strtold("nan(payload)!", &dnan_endp);
  int dnan_errno = *strto_errp;
  *strto_errp = 0;
  long ov_pos = strtol("9223372036854775808!", &ov_pos_endp, 10);
  int ov_pos_errno = *strto_errp;
  *strto_errp = 0;
  long ov_neg = strtol("-9223372036854775809!", &ov_neg_endp, 10);
  int ov_neg_errno = *strto_errp;
  *strto_errp = 0;
  unsigned long uov = strtoul("18446744073709551616!", &uov_endp, 10);
  int uov_errno = *strto_errp;
  *strto_errp = 0;
  unsigned long uneg = strtoul("-1!", &uneg_endp, 10);
  int uneg_errno = *strto_errp;
  *strto_errp = 0;
  long bad_base = strtol(bad_base_src, &bad_base_endp, 1);
  int bad_base_errno = *strto_errp;
  srand(1);
  int rand1 = rand();
  int rand2 = rand();
  div_t divv = div(-7, 3);
  ldiv_t ldivv = ldiv(-7000000000L, 3000000000L);
  lldiv_t lldivv = lldiv(7000000000000LL, -3000000000000LL);
  imaxdiv_t imaxdivv = imaxdiv(-7000000000000LL, 3000000000000LL);
  int nums[5];
  int key = 3;
  int *found;
  int never = 0;
  void *nullv = 0;
  long tloc = 123;
  long now = 0;
  long later = 90061;
  struct tm *tm_info;
  int tm_info_ok;
  struct tm *gmtime_info;
  struct tm mk_time = {0};
  long made_time;
  struct timespec ts_info = {-1, -1};
  int timespec_ok;
  int timespec_bad_base;
  char timebuf[64];
  unsigned long timebuf_len;
  int wtimebuf[64];
  int wtimefmt[16];
  unsigned long wtimebuf_len;
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
  int wxfrm[8];
  int wspn_src[8];
  int wspn_accept[4];
  int wcspn_reject[4];
  int wpbrk_accept[4];
  int wtok_src[12];
  int wtok_delim[2];
  int *wtok_save = 0;
  int *wtok1 = 0;
  int *wtok2 = 0;
  int *wtok3 = 0;
  int *wtok4 = 0;
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
  int wccomma[8];
  int wcllhex[12];
  int wcullhex[10];
  int wcexpf[10];
  int wchexfloat[10];
  int wcinf[5];
  int legacy_wc = 0;
  int legacy_wide[8];
  char legacy_mb[16];
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
  wccomma[0] = '1';
  wccomma[1] = '2';
  wccomma[2] = ',';
  wccomma[3] = '5';
  wccomma[4] = 0;
  wcllhex[0] = '-';
  wcllhex[1] = '8';
  wcllhex[2] = '0';
  wcllhex[3] = '0';
  wcllhex[4] = '0';
  wcllhex[5] = '0';
  wcllhex[6] = '0';
  wcllhex[7] = '0';
  wcllhex[8] = '0';
  wcllhex[9] = '!';
  wcllhex[10] = 0;
  wcullhex[0] = 'f';
  wcullhex[1] = 'f';
  wcullhex[2] = 'f';
  wcullhex[3] = 'f';
  wcullhex[4] = 'f';
  wcullhex[5] = 'f';
  wcullhex[6] = 'f';
  wcullhex[7] = 'f';
  wcullhex[8] = '!';
  wcullhex[9] = 0;
  wcexpf[0] = '-';
  wcexpf[1] = '1';
  wcexpf[2] = '.';
  wcexpf[3] = '2';
  wcexpf[4] = '5';
  wcexpf[5] = 'e';
  wcexpf[6] = '2';
  wcexpf[7] = '!';
  wcexpf[8] = 0;
  wchexfloat[0] = '0';
  wchexfloat[1] = 'x';
  wchexfloat[2] = '1';
  wchexfloat[3] = '.';
  wchexfloat[4] = '8';
  wchexfloat[5] = 'p';
  wchexfloat[6] = '+';
  wchexfloat[7] = '3';
  wchexfloat[8] = '!';
  wchexfloat[9] = 0;
  wcinf[0] = 'i';
  wcinf[1] = 'n';
  wcinf[2] = 'f';
  wcinf[3] = '!';
  wcinf[4] = 0;
  ws[0] = 'A';
  ws[1] = 'b';
  ws[2] = 0;
  wspn_src[0] = 'a';
  wspn_src[1] = 'b';
  wspn_src[2] = 'a';
  wspn_src[3] = 'c';
  wspn_src[4] = 'd';
  wspn_src[5] = 0;
  wspn_accept[0] = 'a';
  wspn_accept[1] = 'b';
  wspn_accept[2] = 0;
  wcspn_reject[0] = 'c';
  wcspn_reject[1] = 0;
  wpbrk_accept[0] = 'c';
  wpbrk_accept[1] = 'd';
  wpbrk_accept[2] = 0;
  wtok_src[0] = 'a';
  wtok_src[1] = 'a';
  wtok_src[2] = ',';
  wtok_src[3] = 'b';
  wtok_src[4] = 'b';
  wtok_src[5] = ',';
  wtok_src[6] = ',';
  wtok_src[7] = 'c';
  wtok_src[8] = 'c';
  wtok_src[9] = 0;
  wtok_delim[0] = ',';
  wtok_delim[1] = 0;
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
    double dummy_d = 0.0;
    float dummy_f = 0.0f;
    long double dummy_ld = 0.0L;
    exit(7);
    quick_exit(8);
    _Exit(9);
    abort();
    fgetwc(nullv);
    getwc(nullv);
    getwchar();
    fputwc('A', nullv);
    putwc('A', nullv);
    putwchar('A');
    ungetwc('A', nullv);
    fgetws(convw, 8, nullv);
    fputws(convw, nullv);
    fwide(nullv, 1);
    fpclassify(0.0);
    isfinite(0.0);
    isinf(0.0);
    isnan(0.0);
    isnormal(1.0);
    signbit(-0.0);
    isgreater(2.0, 1.0);
    isgreaterequal(2.0, 2.0);
    isless(1.0, 2.0);
    islessequal(2.0, 2.0);
    islessgreater(1.0, 2.0);
    isunordered(0.0, 0.0);
    frexp(1.0, &never);
    frexpf(1.0f, &never);
    frexpl(1.0L, &never);
    remainder(5.5, 2.0);
    remainderf(5.5f, 2.0f);
    remainderl(5.5L, 2.0L);
    remquo(5.5, 2.0, &never);
    remquof(5.5f, 2.0f, &never);
    remquol(5.5L, 2.0L, &never);
    fdim(2.0, 1.0);
    fdimf(2.0f, 1.0f);
    fdiml(2.0L, 1.0L);
    fma(2.0, 3.0, 0.5);
    fmaf(2.0f, 3.0f, 0.5f);
    fmal(2.0L, 3.0L, 0.5L);
    ldexp(1.0, 1);
    ldexpf(1.0f, 1);
    ldexpl(1.0L, 1);
    scalbn(1.0, 1);
    scalbnf(1.0f, 1);
    scalbnl(1.0L, 1);
    scalbln(1.0, 1L);
    scalblnf(1.0f, 1L);
    scalblnl(1.0L, 1L);
    ilogb(1.0);
    ilogbf(1.0f);
    ilogbl(1.0L);
    logb(1.0);
    logbf(1.0f);
    logbl(1.0L);
    modf(1.0, &dummy_d);
    modff(1.0f, &dummy_f);
    modfl(1.0L, &dummy_ld);
    copysign(1.0, -0.0);
    copysignf(1.0f, -0.0f);
    copysignl(1.0L, -0.0L);
    nan("");
    nanf("");
    nanl("");
    erf(1.0);
    erff(1.0f);
    erfl(1.0L);
    erfc(1.0);
    erfcf(1.0f);
    erfcl(1.0L);
    nearbyint(1.5);
    nearbyintf(1.5f);
    nearbyintl(1.5L);
    rint(1.5);
    rintf(1.5f);
    rintl(1.5L);
    lrint(1.5);
    lrintf(1.5f);
    lrintl(1.5L);
    llrint(1.5);
    llrintf(1.5f);
    llrintl(1.5L);
    lround(1.5);
    lroundf(1.5f);
    lroundl(1.5L);
    llround(1.5);
    llroundf(1.5f);
    llroundl(1.5L);
    sinh(1.0);
    sinhf(1.0f);
    sinhl(1.0L);
    cosh(1.0);
    coshf(1.0f);
    coshl(1.0L);
    tanh(1.0);
    tanhf(1.0f);
    tanhl(1.0L);
    asinh(1.0);
    asinhf(1.0f);
    asinhl(1.0L);
    acosh(2.0);
    acoshf(2.0f);
    acoshl(2.0L);
    atanh(0.5);
    atanhf(0.5f);
    atanhl(0.5L);
    longjmp(jb, 1);
  }
  int sj = setjmp(jb);
  int usage_ok = getrusage(0, &usage);
  timespec_ok = timespec_get(&ts_info, TIME_UTC);
  timespec_bad_base = timespec_get(&ts_info, 99);
  tm_info = localtime(&now);
  tm_info_ok = tm_info != 0 && tm_info->tm_sec == 0 && tm_info->tm_min == 0 &&
               tm_info->tm_hour == 0 && tm_info->tm_mday == 1 && tm_info->tm_mon == 0 &&
               tm_info->tm_year == 70 && tm_info->tm_wday == 4 && tm_info->tm_yday == 0;
  gmtime_info = gmtime(&later);
  timebuf_len = strftime(timebuf, sizeof(timebuf), "%F %T %a %b %j", gmtime_info);
  wtimefmt[0] = '%';
  wtimefmt[1] = 'F';
  wtimefmt[2] = ' ';
  wtimefmt[3] = '%';
  wtimefmt[4] = 'T';
  wtimefmt[5] = 0;
  wtimebuf_len = wcsftime(wtimebuf, 64, wtimefmt, gmtime_info);
  mk_time.tm_sec = 0;
  mk_time.tm_min = 0;
  mk_time.tm_hour = 0;
  mk_time.tm_mday = 3;
  mk_time.tm_mon = 0;
  mk_time.tm_year = 70;
  mk_time.tm_wday = 0;
  mk_time.tm_yday = 0;
  mk_time.tm_isdst = -1;
  made_time = mktime(&mk_time);
  wcscpy(wd, ws);
  wcsncpy(we, ws, 3);
  wcscat(we, ws);
  wcsncat(we, ws + 1, 1);
  wmemcpy(wfbuf, we, 4);
  wmemmove(wfbuf + 1, wfbuf, 3);
  wmemset(wfbuf + 4, 'Z', 2);
  unsigned long wxfrm_len = wcsxfrm(wxfrm, ws, 8);
  unsigned long wxfrm_need = wcsxfrm(0, we, 0);
  wtok1 = wcstok(wtok_src, wtok_delim, &wtok_save);
  wtok2 = wcstok(0, wtok_delim, &wtok_save);
  wtok3 = wcstok(0, wtok_delim, &wtok_save);
  wtok4 = wcstok(0, wtok_delim, &wtok_save);
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
  int wempty[1];
  wempty[0] = 0;
  int wemptyfmt[1];
  wemptyfmt[0] = 0;
  int wscan_empty_fmt_ret = swscanf(wempty, wemptyfmt);
  int wscanf_n_fmt[3];
  wscanf_n_fmt[0] = '%';
  wscanf_n_fmt[1] = 'n';
  wscanf_n_fmt[2] = 0;
  int wscan_empty_n = 7;
  int wscan_empty_n_ret = swscanf(wempty, wscanf_n_fmt, &wscan_empty_n);
  int wscan_empty_d = 99;
  int wscan_empty_d_ret = swscanf(wempty, scanfmt, &wscan_empty_d);
  int wspacebuf[4];
  wspacebuf[0] = ' ';
  wspacebuf[1] = ' ';
  wspacebuf[2] = ' ';
  wspacebuf[3] = 0;
  int wscan_space_d = 99;
  int wscan_space_d_ret = swscanf(wspacebuf, scanfmt, &wscan_space_d);
  int wxbuf[2];
  wxbuf[0] = 'x';
  wxbuf[1] = 0;
  int wscan_mismatch_d = 99;
  int wscan_mismatch_d_ret = swscanf(wxbuf, scanfmt, &wscan_mismatch_d);
  int wscanf_hh_buf[8];
  wscanf_hh_buf[0] = '-';
  wscanf_hh_buf[1] = '5';
  wscanf_hh_buf[2] = ' ';
  wscanf_hh_buf[3] = '2';
  wscanf_hh_buf[4] = '5';
  wscanf_hh_buf[5] = '0';
  wscanf_hh_buf[6] = 0;
  int wscanf_hh_fmt[10];
  wscanf_hh_fmt[0] = '%';
  wscanf_hh_fmt[1] = 'h';
  wscanf_hh_fmt[2] = 'h';
  wscanf_hh_fmt[3] = 'd';
  wscanf_hh_fmt[4] = ' ';
  wscanf_hh_fmt[5] = '%';
  wscanf_hh_fmt[6] = 'h';
  wscanf_hh_fmt[7] = 'h';
  wscanf_hh_fmt[8] = 'u';
  wscanf_hh_fmt[9] = 0;
  signed char wscan_hh = 0;
  unsigned char wscan_uhh = 0;
  int wscan_hh_ret = swscanf(wscanf_hh_buf, wscanf_hh_fmt, &wscan_hh, &wscan_uhh);
  int wscanf_n_len_buf[9];
  wscanf_n_len_buf[0] = '1';
  wscanf_n_len_buf[1] = '2';
  wscanf_n_len_buf[2] = ' ';
  wscanf_n_len_buf[3] = '3';
  wscanf_n_len_buf[4] = '4';
  wscanf_n_len_buf[5] = '5';
  wscanf_n_len_buf[6] = ' ';
  wscanf_n_len_buf[7] = '6';
  wscanf_n_len_buf[8] = 0;
  int wscanf_n_len_fmt[19];
  wscanf_n_len_fmt[0] = '%';
  wscanf_n_len_fmt[1] = 'd';
  wscanf_n_len_fmt[2] = '%';
  wscanf_n_len_fmt[3] = 'h';
  wscanf_n_len_fmt[4] = 'h';
  wscanf_n_len_fmt[5] = 'n';
  wscanf_n_len_fmt[6] = ' ';
  wscanf_n_len_fmt[7] = '%';
  wscanf_n_len_fmt[8] = 'd';
  wscanf_n_len_fmt[9] = '%';
  wscanf_n_len_fmt[10] = 'h';
  wscanf_n_len_fmt[11] = 'n';
  wscanf_n_len_fmt[12] = ' ';
  wscanf_n_len_fmt[13] = '%';
  wscanf_n_len_fmt[14] = 'd';
  wscanf_n_len_fmt[15] = '%';
  wscanf_n_len_fmt[16] = 'l';
  wscanf_n_len_fmt[17] = 'n';
  wscanf_n_len_fmt[18] = 0;
  int wscan_n_a = 0;
  int wscan_n_b = 0;
  int wscan_n_c = 0;
  signed char wscan_n_hh = 0;
  short wscan_n_h = 0;
  long wscan_n_l = 0;
  int wscan_n_len_ret = swscanf(wscanf_n_len_buf, wscanf_n_len_fmt,
                                &wscan_n_a, &wscan_n_hh, &wscan_n_b, &wscan_n_h,
                                &wscan_n_c, &wscan_n_l);
  int scanpfmt[3];
  scanpfmt[0] = '%';
  scanpfmt[1] = 'p';
  scanpfmt[2] = 0;
  int scanpbuf[5];
  scanpbuf[0] = '0';
  scanpbuf[1] = 'x';
  scanpbuf[2] = '2';
  scanpbuf[3] = 'a';
  scanpbuf[4] = 0;
  void *wscan_p = 0;
  int wscan_p_ret = swscanf(scanpbuf, scanpfmt, &wscan_p);
  int scanbrfmt[8];
  scanbrfmt[0] = '%';
  scanbrfmt[1] = 'l';
  scanbrfmt[2] = '[';
  scanbrfmt[3] = 'a';
  scanbrfmt[4] = '-';
  scanbrfmt[5] = 'z';
  scanbrfmt[6] = ']';
  scanbrfmt[7] = 0;
  int scanbrbuf[5];
  scanbrbuf[0] = 'a';
  scanbrbuf[1] = 'b';
  scanbrbuf[2] = 'c';
  scanbrbuf[3] = '!';
  scanbrbuf[4] = 0;
  int wscan_set[4];
  int wscan_set_ret = swscanf(scanbrbuf, scanbrfmt, wscan_set);
  int wscanf_float_buf[23];
  wscanf_float_buf[0] = '1';
  wscanf_float_buf[1] = '.';
  wscanf_float_buf[2] = '2';
  wscanf_float_buf[3] = '5';
  wscanf_float_buf[4] = ' ';
  wscanf_float_buf[5] = '-';
  wscanf_float_buf[6] = '2';
  wscanf_float_buf[7] = '.';
  wscanf_float_buf[8] = '5';
  wscanf_float_buf[9] = 'e';
  wscanf_float_buf[10] = '1';
  wscanf_float_buf[11] = ' ';
  wscanf_float_buf[12] = '0';
  wscanf_float_buf[13] = 'x';
  wscanf_float_buf[14] = '1';
  wscanf_float_buf[15] = '.';
  wscanf_float_buf[16] = '8';
  wscanf_float_buf[17] = 'p';
  wscanf_float_buf[18] = '+';
  wscanf_float_buf[19] = '3';
  wscanf_float_buf[20] = '!';
  wscanf_float_buf[21] = 0;
  int wscanf_float_fmt[11];
  wscanf_float_fmt[0] = '%';
  wscanf_float_fmt[1] = 'f';
  wscanf_float_fmt[2] = ' ';
  wscanf_float_fmt[3] = '%';
  wscanf_float_fmt[4] = 'l';
  wscanf_float_fmt[5] = 'f';
  wscanf_float_fmt[6] = ' ';
  wscanf_float_fmt[7] = '%';
  wscanf_float_fmt[8] = 'L';
  wscanf_float_fmt[9] = 'f';
  wscanf_float_fmt[10] = 0;
  float wscan_f = 0.0f;
  double wscan_d = 0.0;
  long double wscan_ld = 0.0;
  int wscan_float_ret = swscanf(wscanf_float_buf, wscanf_float_fmt,
                                &wscan_f, &wscan_d, &wscan_ld);
  int wscanf_width_buf[7];
  wscanf_width_buf[0] = '1';
  wscanf_width_buf[1] = '2';
  wscanf_width_buf[2] = '.';
  wscanf_width_buf[3] = '3';
  wscanf_width_buf[4] = '4';
  wscanf_width_buf[5] = 'x';
  wscanf_width_buf[6] = 0;
  int wscanf_width_fmt[4];
  wscanf_width_fmt[0] = '%';
  wscanf_width_fmt[1] = '4';
  wscanf_width_fmt[2] = 'f';
  wscanf_width_fmt[3] = 0;
  float wscan_width_f = 0.0f;
  int wscan_float_width_ret = swscanf(wscanf_width_buf, wscanf_width_fmt, &wscan_width_f);
  int wscanf_comma_buf[5];
  wscanf_comma_buf[0] = '1';
  wscanf_comma_buf[1] = '2';
  wscanf_comma_buf[2] = ',';
  wscanf_comma_buf[3] = '5';
  wscanf_comma_buf[4] = 0;
  int wscanf_comma_fmt[6];
  wscanf_comma_fmt[0] = '%';
  wscanf_comma_fmt[1] = 'l';
  wscanf_comma_fmt[2] = 'f';
  wscanf_comma_fmt[3] = '%';
  wscanf_comma_fmt[4] = 'n';
  wscanf_comma_fmt[5] = 0;
  double wscan_comma_d = 0.0;
  int wscan_comma_n = 0;
  int wscan_comma_ret = swscanf(wscanf_comma_buf, wscanf_comma_fmt,
                                &wscan_comma_d, &wscan_comma_n);
  int wscanf_special_buf[27];
  char *wscanf_special_src = "nan(payload) INF -infinity";
  int wscanf_special_i;
  for (wscanf_special_i = 0; wscanf_special_i < 27; wscanf_special_i++) {
    wscanf_special_buf[wscanf_special_i] = wscanf_special_src[wscanf_special_i];
  }
  float wscan_nan = 0.0f;
  double wscan_inf = 0.0;
  long double wscan_ninf = 0.0;
  int wscan_float_special_ret = swscanf(wscanf_special_buf, wscanf_float_fmt,
                                        &wscan_nan, &wscan_inf, &wscan_ninf);
  if (wscan_float_ret != 3) return 99;
  if ((int)(wscan_f * 100.0f) != 125) return 100;
  if ((int)wscan_d != -25) return 101;
  if ((int)wscan_ld != 12) return 102;
  if (!(wscan_float_width_ret == 1 && (int)(wscan_width_f * 10.0f) == 123)) return 103;
  if (!(wscan_comma_ret == 1 && (int)wscan_comma_d == 12 && wscan_comma_n == 2)) return 118;
  if (wscan_float_special_ret != 3) return 104;
  if (!(wscan_nan != 0.0f && !(wscan_nan < 0.0f) && !(wscan_nan > 0.0f))) return 105;
  if (!(wscan_inf > 1000000.0 && wscan_ninf < -1000000.0L)) return 106;
  int scan_i = 0;
  unsigned int scan_x = 0;
  char scan_s[4];
  char scan_c[2];
  int scan_n = 0;
  int ssret = sscanf(" -42 2a abcZ", "%d %x %3s%c%n", &scan_i, &scan_x, scan_s, scan_c, &scan_n);
  signed char scan_hh = 0;
  unsigned char scan_uhh = 0;
  int ssret_hh = sscanf("-5 250", "%hhd %hhu", &scan_hh, &scan_uhh);
  int scan_n_a = 0;
  int scan_n_b = 0;
  int scan_n_c = 0;
  signed char scan_n_hh = 0;
  short scan_n_h = 0;
  long scan_n_l = 0;
  int ssret_n_len = sscanf("12 345 6", "%d%hhn %d%hn %d%ln",
                           &scan_n_a, &scan_n_hh, &scan_n_b, &scan_n_h,
                           &scan_n_c, &scan_n_l);
  int scan_ws[3];
  int scan_wc[1];
  int ssret_wide_char = sscanf("hi Z", "%ls %lc", scan_ws, scan_wc);
  void *scan_p = 0;
  int ssret_p = sscanf("0x2a", "%p", &scan_p);
  char scan_set[4];
  char scan_not_z[4];
  int ssret_set = sscanf("abc123Z", "%3[a-z]%3[^Z]", scan_set, scan_not_z);
  float scan_f = 0.0f;
  double scan_d = 0.0;
  long double scan_ld = 0.0;
  int ssret_float = sscanf("1.25 -2.5e1 0x1.8p+3!", "%f %lf %Lf",
                           &scan_f, &scan_d, &scan_ld);
  float scan_width_f = 0.0f;
  int ssret_float_width = sscanf("12.34x", "%4f", &scan_width_f);
  float scan_nan = 0.0f;
  double scan_inf = 0.0;
  long double scan_ninf = 0.0;
  int ssret_float_special = sscanf("nan(payload) INF -infinity", "%f %lf %Lf",
                                   &scan_nan, &scan_inf, &scan_ninf);
  double scan_inf_width = 0.0;
  int scan_inf_width_n = 0;
  int ssret_inf_width = sscanf("infinity", "%3lf%n", &scan_inf_width, &scan_inf_width_n);
  double scan_comma_d = 0.0;
  int scan_comma_n = 0;
  int ssret_comma = sscanf("12,5", "%lf%n", &scan_comma_d, &scan_comma_n);
  if (ssret_float != 3) return 92;
  if ((int)(scan_f * 100.0f) != 125) return 96;
  if ((int)scan_d != -25) return 97;
  if ((int)scan_ld != 12) return 98;
  if (!(ssret_float_width == 1 && (int)(scan_width_f * 10.0f) == 123)) return 93;
  if (ssret_float_special != 3) return 94;
  if (!(scan_nan != 0.0f && !(scan_nan < 0.0f) && !(scan_nan > 0.0f))) return 95;
  if (!(scan_inf > 1000000.0 && scan_ninf < -1000000.0L)) return 107;
  if (!(ssret_inf_width == 1 && scan_inf_width > 1000000.0 && scan_inf_width_n == 3)) return 108;
  if (!(ssret_comma == 1 && (int)scan_comma_d == 12 && scan_comma_n == 2)) return 119;
  int ssret_empty_fmt = sscanf("", "");
  int scan_empty_n = 7;
  int ssret_empty_n = sscanf("", "%n", &scan_empty_n);
  int scan_empty_d = 99;
  int ssret_empty_d = sscanf("", "%d", &scan_empty_d);
  int scan_space_d = 99;
  int ssret_space_d = sscanf("   ", "%d", &scan_space_d);
  int scan_mismatch_d = 99;
  int ssret_mismatch_d = sscanf("x", "%d", &scan_mismatch_d);
  if (!(ssret_empty_fmt == 0 && ssret_empty_n == 0 && scan_empty_n == 0)) return 109;
  if (!(ssret_empty_d == -1 && scan_empty_d == 99)) return 110;
  if (!(ssret_space_d == -1 && scan_space_d == 99)) return 111;
  if (!(ssret_mismatch_d == 0 && scan_mismatch_d == 99)) return 112;
  if (!(wscan_empty_fmt_ret == 0 && wscan_empty_n_ret == 0 && wscan_empty_n == 0)) return 113;
  if (!(wscan_empty_d_ret == -1 && wscan_empty_d == 99 &&
        wscan_space_d_ret == -1 && wscan_space_d == 99 &&
        wscan_mismatch_d_ret == 0 && wscan_mismatch_d == 99)) return 114;
  if (!(wscan_hh_ret == 2 && wscan_hh == -5 && wscan_uhh == 250)) return 116;
  if (!(wscan_n_len_ret == 3 && wscan_n_a == 12 && wscan_n_hh == 2 &&
        wscan_n_b == 345 && wscan_n_h == 6 && wscan_n_c == 6 && wscan_n_l == 8)) return 117;
  struct lconv *lc;
  setlocale(0, "C");
  lc = localeconv();
  FILE *empty_wf = fopen("empty.txt", "w");
  int empty_close_w = fclose(empty_wf);
  FILE *empty_rf = fopen("empty.txt", "r");
  int fscan_empty_fmt_ret = fscanf(empty_rf, "");
  int fscan_empty_fmt_eof = feof(empty_rf);
  int fscan_empty_n = 7;
  int fscan_empty_n_ret = fscanf(empty_rf, "%n", &fscan_empty_n);
  int fscan_empty_n_eof = feof(empty_rf);
  int fscan_empty_d = 99;
  int fscan_empty_d_ret = fscanf(empty_rf, "%d", &fscan_empty_d);
  int fscan_empty_eof = feof(empty_rf);
  int empty_close_r = fclose(empty_rf);
  if (!(empty_close_w == 0 && empty_close_r == 0 &&
        fscan_empty_fmt_ret == 0 && fscan_empty_n_ret == 0 && fscan_empty_n == 0 &&
        !fscan_empty_fmt_eof && !fscan_empty_n_eof &&
        fscan_empty_d_ret == -1 && fscan_empty_d == 99 && fscan_empty_eof)) return 115;
  FILE *frw = fopen("tmp.txt", "w");
  int frw_open = frw != 0;
  int frw_reopen = frw_open && freopen("tmp.txt", "w+", frw) == frw;
  int frw_write = frw_reopen && fwrite("R", 1, 1, frw) == 1;
  int frw_seek = frw_reopen && fseek(frw, 0, 0) == 0;
  char frw_buf[2];
  int frw_read = frw_reopen && fread(frw_buf, 1, 1, frw) == 1 && frw_buf[0] == 'R';
  int frw_close = frw_open ? fclose(frw) : -1;
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
  FILE *scanw = fopen("tmp.txt", "w");
  fwrite("55 2a okZ", 1, 9, scanw);
  fclose(scanw);
  FILE *scanr = fopen("tmp.txt", "r");
  int file_scan_i = 0;
  unsigned int file_scan_x = 0;
  char file_scan_s[3];
  char file_scan_c[2];
  int file_scan_n = 0;
  int fsret = fscanf(scanr, "%d %x %2s%c%n", &file_scan_i, &file_scan_x,
                     file_scan_s, file_scan_c, &file_scan_n);
  long file_scan_pos = ftell(scanr);
  fclose(scanr);
  int stdin_scan_i = 0;
  int stdin_scan_empty = scanf("%d", &stdin_scan_i);
  FILE *scan_restore = fopen("tmp.txt", "w");
  fwrite("A\nB", 1, 3, scan_restore);
  fclose(scan_restore);
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
  int putc_file_ret = putc('J', fpw);
  long pos_after_fputs = ftell(fpw);
  fclose(fpw);
  FILE *fpr = fopen("tmp.txt", "r");
  char fpbuf[4];
  unsigned long fpread = fread(fpbuf, 1, 3, fpr);
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
  char vsprintfbuf[8];
  int vsprintf_ret = call_vsprintf(vsprintfbuf, "%d-%s", 12, "go");
  int vprintf_ret = call_vprintf("P%d", 6);
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
                          putc_file_ret == 'J' && pos_after_fputs == 3 && fpread == 3 &&
                          fpbuf[0] == 'H' && fpbuf[1] == 'I' && fpbuf[2] == 'J' &&
                          fp_eof == -1;
  int fprintf_file_ok = fmt_file_ret == 2 && fmt_file_pos == 2 &&
                        fmtread == 2 && fmtbuf[0] == 'K' &&
                        fmtbuf[1] == '7' && fmt_eof == -1;
  int vformat_ok = vfmt_ret == 5 && vfmtbuf[0] == '3' && vfmtbuf[1] == '1' &&
                   vfmtbuf[2] == '-' && vfmtbuf[3] == 'o' && vfmtbuf[4] == 'k' &&
                   vfmtbuf[5] == 0 &&
                   vsprintf_ret == 5 && vsprintfbuf[0] == '1' && vsprintfbuf[1] == '2' &&
                   vsprintfbuf[2] == '-' && vsprintfbuf[3] == 'g' && vsprintfbuf[4] == 'o' &&
                   vsprintfbuf[5] == 0 && vprintf_ret == 2 &&
                   vfprintf_ret == 2 && vfprintf_pos == 2;
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
  double math_zero = 0.0;
  double math_nan = math_zero / math_zero;
  double math_inf = 1.0 / math_zero;
  double math_nzero = -math_zero;
  double math_subnormal = 1.0e-310;
  int math_class_ok = fpclassify(math_nan) == 0 &&
                      fpclassify(math_inf) == 1 &&
                      fpclassify(0.0) == 2 &&
                      fpclassify(math_subnormal) == 3 &&
                      fpclassify(1.0) == 4 &&
                      isnan(math_nan) && !isnan(1.0) &&
                      isinf(math_inf) && !isinf(math_nan) &&
                      isfinite(1.0) && !isfinite(math_inf) && !isfinite(math_nan) &&
                      isnormal(1.0) && !isnormal(math_subnormal) &&
                      signbit(-1.0) && signbit(math_nzero) && !signbit(0.0) &&
                      isgreater(2.0, 1.0) && !isgreater(math_nan, 1.0) &&
                      isgreaterequal(2.0, 2.0) &&
                      isless(1.0, 2.0) && !isless(math_nan, 2.0) &&
                      islessequal(2.0, 2.0) &&
                      islessgreater(1.0, 2.0) && !islessgreater(2.0, 2.0) &&
                      isunordered(math_nan, 1.0) && !isunordered(1.0, 2.0);
  int math_decomp_ok = math_decomp_check();
  int math_exp_log_ext_ok = math_exp_log_ext_check();
  int math_round_ext_ok = math_round_ext_check();
  char *pbrk_src = "xyzabc";
  unsigned long span = strspn("aabbc", "ab");
  unsigned long cspan = strcspn("aabbc", "c");
  char *pbrk = strpbrk(pbrk_src, "ba");
  char *pbrk_none = strpbrk("xyz", "ab");
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
         strcoll("abc", "abd") < 0 && strcoll("same", "same") == 0 &&
         xfrm_len == 5 && strcmp(xfrm, "hello") == 0 && xfrm_len_only == 6 &&
         span == 4 && cspan == 4 && pbrk == pbrk_src + 3 && pbrk_none == 0 &&
         tok1 == toks && strcmp(tok1, "aa") == 0 &&
         strcmp(tok2, "bb") == 0 && strcmp(tok3, "cc") == 0 && tok4 == 0 &&
         strerror(5)[0] == 'e' &&
         abs(-42) == 42 &&
         labs(-1234567890123L) == 1234567890123L &&
         llabs(-1234567890123LL) == 1234567890123LL &&
         imaxabs(-1234567890123L) == 1234567890123L &&
         atol(" -1234x") == -1234 &&
         atoll(" -1234567890123x") == -1234567890123LL &&
         parsed == -42 && *endp == 0 &&
         parsed_u == 255 && *uendp == '!' &&
         (int)parsed_d == -125 && *dendp == '!' &&
         (int)(parsed_f * 100.0f) == 25 && *fendp == '!' &&
         (int)parsed_ld == 12 && *ldendp == '!' &&
         (int)comma_d == 12 && *comma_endp == ',' &&
         (int)(parsed_atof * 100.0) == 325 &&
         parsed_imax == 127 && *imax_endp == '!' &&
         parsed_umax == 255 && *umax_endp == '!' &&
         parsed_ll == -2147483648LL && *ll_endp == '!' &&
         parsed_ull == 4294967295ULL && *ull_endp == '!' &&
         dov > 1e300 && *dov_endp == '!' && dov_errno == 34 &&
         dunder == 0.0 && *dunder_endp == '!' && dunder_errno == 34 &&
         dzero == 0.0 && *dzero_endp == '!' && dzero_errno == 0 &&
         dinf > 1e300 && *dinf_endp == '!' && dinf_errno == 0 &&
         dninf < -1e30f && *dninf_endp == '!' && dninf_errno == 0 &&
         dnan != 0.0L && !(dnan < 0.0L) && !(dnan > 0.0L) && *dnan_endp == '!' && dnan_errno == 0 &&
         ov_pos == 9223372036854775807L && *ov_pos_endp == '!' && ov_pos_errno == 34 &&
         ov_neg == (-9223372036854775807L - 1L) && *ov_neg_endp == '!' && ov_neg_errno == 34 &&
         uov == ~0UL && *uov_endp == '!' && uov_errno == 34 &&
         uneg == ~0UL && *uneg_endp == '!' && uneg_errno == 0 &&
         bad_base == 0 && bad_base_endp == bad_base_src && bad_base_errno == 22 &&
         rand1 != rand2 &&
         divv.quot == -2 && divv.rem == -1 &&
         ldivv.quot == -2 && ldivv.rem == -1000000000L &&
         lldivv.quot == -2 && lldivv.rem == 1000000000000LL &&
         imaxdivv.quot == -2 && imaxdivv.rem == -1000000000000LL &&
         aligned16 != 0 && aligned32 != 0 && ((long)aligned16 % 16) == 0 &&
         ((long)aligned32 % 32) == 0 && aligned16[0] == 'A' && aligned16[31] == 'Z' &&
         bad_aligned_nonpow2 == 0 && bad_aligned_size == 0 &&
         atexit(nullv) == 0 && at_quick_exit(nullv) == 0 &&
         getenv("AGC_MISSING_ENV") == 0 &&
         resolved_pathp == resolved_path && strcmp(resolved_path, "include") == 0 &&
         resolved_nullp != 0 && resolved_nullp != "src" && strcmp(resolved_nullp, "src") == 0 &&
         system("true") == 0 &&
         frw_open && frw_reopen && frw_write && frw_seek && frw_read && frw_close == 0 &&
         nums[0] == 1 && nums[1] == 2 && nums[2] == 3 && nums[3] == 4 && nums[4] == 5 &&
         found == nums + 2 && *found == 3 &&
         time(&tloc) == 0 && tloc == 0 && clock() == 0 &&
         (int)difftime(100, 58) == 42 &&
         timespec_ok == TIME_UTC && ts_info.tv_sec == 0 && ts_info.tv_nsec == 0 &&
         timespec_bad_base == 0 &&
         tm_info_ok &&
         gmtime_info != 0 && gmtime_info->tm_sec == 1 && gmtime_info->tm_min == 1 &&
         gmtime_info->tm_hour == 1 && gmtime_info->tm_mday == 2 && gmtime_info->tm_mon == 0 &&
         gmtime_info->tm_year == 70 && gmtime_info->tm_wday == 5 && gmtime_info->tm_yday == 1 &&
         strcmp(asctime(gmtime_info), "Fri Jan  2 01:01:01 1970\n") == 0 &&
         strcmp(ctime(&later), "Fri Jan  2 01:01:01 1970\n") == 0 &&
         timebuf_len == 31 && strcmp(timebuf, "1970-01-02 01:01:01 Fri Jan 002") == 0 &&
         strftime(timebuf, 8, "%Y-%m-%d", gmtime_info) == 0 &&
         wtimebuf_len == 19 && wtimebuf[0] == '1' && wtimebuf[3] == '0' &&
         wtimebuf[4] == '-' && wtimebuf[10] == ' ' && wtimebuf[18] == '1' &&
         wtimebuf[19] == 0 && wcsftime(wtimebuf, 8, wtimefmt, gmtime_info) == 0 &&
         made_time == 172800 && mk_time.tm_wday == 6 && mk_time.tm_yday == 2 &&
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
         wcscoll(ws, we) < 0 && wxfrm_len == 2 && wmemcmp(wxfrm, ws, 3) == 0 &&
         wxfrm_need == 5 &&
         wcsspn(wspn_src, wspn_accept) == 3 && wcscspn(wspn_src, wcspn_reject) == 3 &&
         wcspbrk(wspn_src, wpbrk_accept) == wspn_src + 3 &&
         wtok1 == wtok_src && wcsncmp(wtok1, ws + 0, 0) == 0 &&
         wtok1[0] == 'a' && wtok1[1] == 'a' && wtok1[2] == 0 &&
         wtok2[0] == 'b' && wtok2[1] == 'b' && wtok2[2] == 0 &&
         wtok3[0] == 'c' && wtok3[1] == 'c' && wtok3[2] == 0 && wtok4 == 0 &&
         wmemcmp(wd, ws, 3) == 0 && wmemchr(wfbuf, 'Z', 6) == wfbuf + 4 &&
         wcstol(wcnum, &wend, 16) == -42 && *wend == '.' &&
         wcstoul(wcnum + 2, &wend, 16) == 42 && *wend == '.' &&
         wcstoll(wcllhex, &wend, 16) == -2147483648LL && *wend == '!' &&
         wcstoull(wcullhex, &wend, 16) == 4294967295ULL && *wend == '!' &&
         (int)wcstof(wcexpf, &wend) == -125 && *wend == '!' &&
         (int)(wcstod(wcdec, &wend) * 10.0) == 25 && *wend == 0 &&
         (int)wcstod(wccomma, &wend) == 12 && *wend == ',' &&
         (int)wcstold(wchexfloat, &wend) == 12 && *wend == '!' &&
         wcstod(wcinf, &wend) > 1e300 && *wend == '!' &&
         mblen("Q", 2) == 1 && mbtowc(&legacy_wc, "Q", 2) == 1 && legacy_wc == 'Q' &&
         wctomb(legacy_mb, 'R') == 1 && legacy_mb[0] == 'R' &&
         mbstowcs(legacy_wide, "Hi", 8) == 2 && legacy_wide[0] == 'H' &&
         legacy_wide[1] == 'i' && wcstombs(legacy_mb, legacy_wide, 8) == 2 &&
         legacy_mb[0] == 'H' && legacy_mb[1] == 'i' &&
         mbrtowc(convw, "Q", 2, nullv) == 1 && convw[0] == 'Q' &&
         mbrlen("Q", 2, nullv) == 1 && mbrlen("", 1, nullv) == 0 && mbsinit(nullv) == 1 &&
         wcrtomb(convc, 'R', nullv) == 1 && convc[0] == 'R' &&
         convw[0] == 'Q' && convc[0] == 'R' &&
         m16 == 1 && c16 == 'U' && r16 == 1 &&
         m32 == 1 && c32 == 'V' && r32 == 1 && convc[2] == 'U' && convc[3] == 'V' &&
         btowc('S') == 'S' && wctob('T') == 'T' &&
         swret == 5 && swbuf[0] == '1' && swbuf[1] == '2' && swbuf[2] == '-' &&
         swbuf[3] == 'O' && swbuf[4] == 'K' && swbuf[5] == 0 &&
         scanret == 1 && never == 12 &&
         wscan_empty_fmt_ret == 0 && wscan_empty_n_ret == 0 && wscan_empty_n == 0 &&
         wscan_empty_d_ret == -1 && wscan_empty_d == 99 &&
         wscan_space_d_ret == -1 && wscan_space_d == 99 &&
         wscan_mismatch_d_ret == 0 && wscan_mismatch_d == 99 &&
         wscan_hh_ret == 2 && wscan_hh == -5 && wscan_uhh == 250 &&
         wscan_n_len_ret == 3 && wscan_n_a == 12 && wscan_n_hh == 2 &&
         wscan_n_b == 345 && wscan_n_h == 6 && wscan_n_c == 6 && wscan_n_l == 8 &&
         wscan_p_ret == 1 && (long)wscan_p == 42 &&
         wscan_set_ret == 1 && wscan_set[0] == 'a' && wscan_set[1] == 'b' &&
         wscan_set[2] == 'c' && wscan_set[3] == 0 &&
         wscan_float_ret == 3 && (int)(wscan_f * 100.0f) == 125 &&
         (int)wscan_d == -25 && (int)wscan_ld == 12 &&
         wscan_float_width_ret == 1 && (int)(wscan_width_f * 10.0f) == 123 &&
         wscan_comma_ret == 1 && (int)wscan_comma_d == 12 && wscan_comma_n == 2 &&
         wscan_float_special_ret == 3 &&
         wscan_nan != 0.0f && !(wscan_nan < 0.0f) && !(wscan_nan > 0.0f) &&
         wscan_inf > 1000000.0 && wscan_ninf < -1000000.0L &&
         ssret == 4 && scan_i == -42 && scan_x == 42 &&
         scan_s[0] == 'a' && scan_s[1] == 'b' && scan_s[2] == 'c' && scan_s[3] == 0 &&
         scan_c[0] == 'Z' && scan_n == 12 &&
         ssret_hh == 2 && scan_hh == -5 && scan_uhh == 250 &&
         ssret_n_len == 3 && scan_n_a == 12 && scan_n_hh == 2 &&
         scan_n_b == 345 && scan_n_h == 6 && scan_n_c == 6 && scan_n_l == 8 &&
         ssret_wide_char == 2 && scan_ws[0] == 'h' && scan_ws[1] == 'i' &&
         scan_ws[2] == 0 && scan_wc[0] == 'Z' &&
         ssret_p == 1 && (long)scan_p == 42 &&
         ssret_set == 2 &&
         scan_set[0] == 'a' && scan_set[1] == 'b' && scan_set[2] == 'c' && scan_set[3] == 0 &&
         scan_not_z[0] == '1' && scan_not_z[1] == '2' && scan_not_z[2] == '3' &&
         scan_not_z[3] == 0 &&
         ssret_float == 3 && (int)(scan_f * 100.0f) == 125 &&
         (int)scan_d == -25 && (int)scan_ld == 12 &&
         ssret_float_width == 1 && (int)(scan_width_f * 10.0f) == 123 &&
         ssret_comma == 1 && (int)scan_comma_d == 12 && scan_comma_n == 2 &&
         ssret_float_special == 3 &&
         scan_nan != 0.0f && !(scan_nan < 0.0f) && !(scan_nan > 0.0f) &&
         scan_inf > 1000000.0 && scan_ninf < -1000000.0L &&
         ssret_inf_width == 1 && scan_inf_width > 1000000.0 && scan_inf_width_n == 3 &&
         ssret_empty_fmt == 0 && ssret_empty_n == 0 && scan_empty_n == 0 &&
         ssret_empty_d == -1 && scan_empty_d == 99 &&
         ssret_space_d == -1 && scan_space_d == 99 &&
         ssret_mismatch_d == 0 && scan_mismatch_d == 99 &&
         empty_close_w == 0 && empty_close_r == 0 &&
         fscan_empty_fmt_ret == 0 && fscan_empty_n_ret == 0 && fscan_empty_n == 0 &&
         !fscan_empty_fmt_eof && !fscan_empty_n_eof &&
         fscan_empty_d_ret == -1 && fscan_empty_d == 99 && fscan_empty_eof &&
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
         isnan(sqrt(-1.0)) &&
         isnan(sqrtf(-1.0f)) &&
         isnan(sqrtl(-1.0L)) &&
         isnan(sqrt(math_nan)) &&
         signbit(sqrt(-math_zero)) &&
         sqrt(math_inf) > 1.0e300 &&
         (int)pow(2.0, 10.0) == 1024 &&
         pow_int == -8000 &&
         pow_frac >= 2998 && pow_frac <= 3002 &&
         powf_int == 32000 &&
         powl_int == 16000 &&
         isnan(pow(-2.0, 0.5)) &&
         isnan(powf(-2.0f, 0.5f)) &&
         isnan(powl(-2.0L, 0.5L)) &&
         isnan(pow(math_nan, 2.0)) &&
         (int)pow(math_nan, 0.0) == 1 &&
         pow(math_zero, -0.5) > 1.0e300 &&
         signbit(pow(-math_zero, 3.0)) &&
         pow(-math_zero, -3.0) < -1.0e300 &&
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
         isnan(sin(math_inf)) &&
         isnan(sinf((float)math_inf)) &&
         isnan(sinl((long double)math_inf)) &&
         isnan(cos(math_inf)) &&
         isnan(cosf((float)math_inf)) &&
         isnan(cosl((long double)math_inf)) &&
         isnan(tan(math_inf)) &&
         isnan(tanf((float)math_inf)) &&
         isnan(tanl((long double)math_inf)) &&
         fmod_pos == 1500 && fmod_neg == -1500 &&
         fmodf_pos == 1500 &&
         fmodl_pos == 1500 &&
         isnan(fmod(7.5, math_zero)) &&
         isnan(fmod(math_inf, 2.0)) &&
         (int)fmod(7.5, math_inf) == 7 &&
         signbit(fmod(-math_zero, 3.0)) &&
         isnan(fmodf(7.5f, 0.0f)) &&
         isnan(fmodl(7.5L, 0.0L)) &&
         cbrt_pos >= 2998 && cbrt_pos <= 3002 &&
         cbrt_neg >= -2002 && cbrt_neg <= -1998 &&
         cbrtf_pos >= 2998 && cbrtf_pos <= 3002 &&
         cbrtl_neg >= -2002 && cbrtl_neg <= -1998 &&
         cbrt(math_inf) > 1.0e300 &&
         cbrt(-math_inf) < -1.0e300 &&
         isnan(cbrt(math_nan)) &&
         isnan(cbrtf((float)math_nan)) &&
         cbrtl((long double)math_inf) > 1.0e300L &&
         signbit(cbrt(-math_zero)) &&
         exp1 >= 2716 && exp1 <= 2720 &&
         expf1 >= 2716 && expf1 <= 2720 &&
         expl1 >= 2716 && expl1 <= 2720 &&
         exp(math_inf) > 1.0e300 &&
         exp(-math_inf) == 0.0 &&
         isnan(exp(math_nan)) &&
         isnan(expf((float)math_nan)) &&
         isnan(expl((long double)math_nan)) &&
         exp2(math_inf) > 1.0e300 &&
         exp2(-math_inf) == 0.0 &&
         expm1(math_inf) > 1.0e300 &&
         (int)expm1(-math_inf) == -1 &&
         loge >= 998 && loge <= 1002 &&
         logfe >= 998 && logfe <= 1002 &&
         logle >= 998 && logle <= 1002 &&
         log(math_zero) < -1.0e300 &&
         log(math_inf) > 1.0e300 &&
         isnan(log(-1.0)) &&
         isnan(logf(-1.0f)) &&
         isnan(logl(-1.0L)) &&
         isnan(log(math_nan)) &&
         log2v >= 2998 && log2v <= 3002 &&
         log2fv >= 2998 && log2fv <= 3002 &&
         log2lv >= 2998 && log2lv <= 3002 &&
         log2(math_zero) < -1.0e300 &&
         isnan(log2(-1.0)) &&
         log10v >= 1998 && log10v <= 2002 &&
         log10fv >= 1998 && log10fv <= 2002 &&
         log10lv >= 1998 && log10lv <= 2002 &&
         log10(math_inf) > 1.0e300 &&
         isnan(log10(-1.0)) &&
         log1p(-1.0) < -1.0e300 &&
         isnan(log1p(-2.0)) &&
         atan1 >= 783 && atan1 <= 787 &&
         atanf1 >= 783 && atanf1 <= 787 &&
         atanl1 >= 783 && atanl1 <= 787 &&
         atan2v >= 1568 && atan2v <= 1572 &&
         atan2fv >= 1568 && atan2fv <= 1572 &&
         atan2lv >= 1568 && atan2lv <= 1572 &&
         signbit(atan2(-math_zero, math_zero)) &&
         (int)(atan2(math_zero, -math_zero) * 1000.0) >= 3140 &&
         (int)(atan2(math_zero, -math_zero) * 1000.0) <= 3143 &&
         (int)(atan2(-math_zero, -math_zero) * 1000.0) <= -3140 &&
         (int)(atan2(-math_zero, -math_zero) * 1000.0) >= -3143 &&
         (int)(atan2(math_inf, math_inf) * 1000.0) >= 783 &&
         (int)(atan2(math_inf, math_inf) * 1000.0) <= 787 &&
         (int)(atan2(math_inf, -math_inf) * 1000.0) >= 2354 &&
         (int)(atan2(math_inf, -math_inf) * 1000.0) <= 2358 &&
         (int)(atan2(-math_inf, -math_inf) * 1000.0) <= -2354 &&
         (int)(atan2(-math_inf, -math_inf) * 1000.0) >= -2358 &&
         asinv >= 1568 && asinv <= 1572 &&
         asinfv >= 1568 && asinfv <= 1572 &&
         asinlv >= 1568 && asinlv <= 1572 &&
         acosv >= 1568 && acosv <= 1572 &&
         acosfv >= 1568 && acosfv <= 1572 &&
         acosl_v >= 1568 && acosl_v <= 1572 &&
         isnan(asin(2.0)) &&
         isnan(asinf(2.0f)) &&
         isnan(asinl(2.0L)) &&
         isnan(asin(math_nan)) &&
         isnan(acos(2.0)) &&
         isnan(acosf(2.0f)) &&
         isnan(acosl(2.0L)) &&
         isnan(acos(math_nan)) &&
         (int)(hypot(3.0, 4.0) * 1000.0) == 5000 &&
         (int)(hypotf(3.0f, 4.0f) * 1000.0f) == 5000 &&
         (int)(hypotl(3.0L, 4.0L) * 1000.0L) == 5000 &&
         hypot(1.0e200, 1.0e200) > 1.0e200 &&
         hypot(math_inf, math_nan) > 1.0e300 &&
         isnan(hypot(math_nan, 3.0)) &&
         (int)(fmin(3.0, 4.0) * 1000.0) == 3000 &&
         (int)(fminf(3.0f, 4.0f) * 1000.0f) == 3000 &&
         (int)(fminl(3.0L, 4.0L) * 1000.0L) == 3000 &&
         (int)fmin(math_nan, 7.0) == 7 &&
         (int)fmin(7.0, math_nan) == 7 &&
         (int)fminf((float)math_nan, 5.0f) == 5 &&
         (int)fminl(6.0L, (long double)math_nan) == 6 &&
         signbit(fmin(-math_zero, math_zero)) &&
         signbit(fmin(math_zero, -math_zero)) &&
         (int)(fmax(3.0, 4.0) * 1000.0) == 4000 &&
         (int)(fmaxf(3.0f, 4.0f) * 1000.0f) == 4000 &&
         (int)(fmaxl(3.0L, 4.0L) * 1000.0L) == 4000 &&
         (int)fmax(math_nan, 7.0) == 7 &&
         (int)fmax(7.0, math_nan) == 7 &&
         (int)fmaxf((float)math_nan, 5.0f) == 5 &&
         (int)fmaxl(6.0L, (long double)math_nan) == 6 &&
         !signbit(fmax(-math_zero, math_zero)) &&
         !signbit(fmax(math_zero, -math_zero)) &&
         math_class_ok &&
         math_decomp_ok &&
         math_exp_log_ext_ok &&
         math_round_ext_ok &&
         sinh0 == 0 && cosh0 >= 998 && cosh0 <= 1002 &&
         tanh0 == 0 && tanh1 >= 759 && tanh1 <= 763 &&
         sinh(math_inf) > 1.0e300 &&
         sinh(-math_inf) < -1.0e300 &&
         cosh(math_inf) > 1.0e300 &&
         cosh(-math_inf) > 1.0e300 &&
         (int)tanh(math_inf) == 1 &&
         (int)tanh(-math_inf) == -1 &&
         acosh(math_inf) > 1.0e300 &&
         isnan(acosh(0.5)) &&
         isnan(acoshf(0.5f)) &&
         isinf(atanh(1.0)) &&
         isinf(atanh(-1.0)) &&
         isnan(atanh(2.0)) &&
         isnan(atanhl(2.0L)) &&
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
         fsret == 4 && file_scan_i == 55 && file_scan_x == 42 &&
         file_scan_s[0] == 'o' && file_scan_s[1] == 'k' && file_scan_s[2] == 0 &&
         file_scan_c[0] == 'Z' && file_scan_n == 9 && file_scan_pos == 9 &&
         stdin_scan_empty == -1 && stdin_scan_i == 0 &&
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
         putc('S', 0) == 'S' &&
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

"$root/build/ag_c_wasm" -c -o "$out_dir/snprintf_length_mods.o" "$out_dir/snprintf_length_mods.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_snprintf_length_mods.wasm" \
  "$out_dir/snprintf_length_mods.o"
wasm-validate "$out_dir/linked_snprintf_length_mods.wasm"
wasm-interp "$out_dir/linked_snprintf_length_mods.wasm" --run-all-exports > "$out_dir/linked_snprintf_length_mods.interp"
grep -q 'main() => i32:42' "$out_dir/linked_snprintf_length_mods.interp"

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

"$root/build/ag_c_wasm" -c -o "$out_dir/vscan_state.o" "$out_dir/vscan_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_vscan_state.wasm" \
  "$out_dir/vscan_state.o"
wasm-validate "$out_dir/linked_vscan_state.wasm"
wasm-interp "$out_dir/linked_vscan_state.wasm" --run-all-exports > "$out_dir/linked_vscan_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_vscan_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/wide_locale_state.o" "$out_dir/wide_locale_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_wide_locale_state.wasm" \
  "$out_dir/wide_locale_state.o"
wasm-validate "$out_dir/linked_wide_locale_state.wasm"
wasm-interp "$out_dir/linked_wide_locale_state.wasm" --run-all-exports > "$out_dir/linked_wide_locale_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_wide_locale_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/locale_state.o" "$out_dir/locale_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_locale_state.wasm" \
  "$out_dir/locale_state.o"
wasm-validate "$out_dir/linked_locale_state.wasm"
wasm-interp "$out_dir/linked_locale_state.wasm" --run-all-exports > "$out_dir/linked_locale_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_locale_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/remove_state.o" "$out_dir/remove_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_remove_state.wasm" \
  "$out_dir/remove_state.o"
wasm-validate "$out_dir/linked_remove_state.wasm"
wasm-interp "$out_dir/linked_remove_state.wasm" --run-all-exports > "$out_dir/linked_remove_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_remove_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/freopen_state.o" "$out_dir/freopen_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_freopen_state.wasm" \
  "$out_dir/freopen_state.o"
wasm-validate "$out_dir/linked_freopen_state.wasm"
wasm-interp "$out_dir/linked_freopen_state.wasm" --run-all-exports > "$out_dir/linked_freopen_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_freopen_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/ungetc_state.o" "$out_dir/ungetc_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_ungetc_state.wasm" \
  "$out_dir/ungetc_state.o"
wasm-validate "$out_dir/linked_ungetc_state.wasm"
wasm-interp "$out_dir/linked_ungetc_state.wasm" --run-all-exports > "$out_dir/linked_ungetc_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_ungetc_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/setvbuf_state.o" "$out_dir/setvbuf_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_setvbuf_state.wasm" \
  "$out_dir/setvbuf_state.o"
wasm-validate "$out_dir/linked_setvbuf_state.wasm"
wasm-interp "$out_dir/linked_setvbuf_state.wasm" --run-all-exports > "$out_dir/linked_setvbuf_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_setvbuf_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/fpos_state.o" "$out_dir/fpos_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_fpos_state.wasm" \
  "$out_dir/fpos_state.o"
wasm-validate "$out_dir/linked_fpos_state.wasm"
wasm-interp "$out_dir/linked_fpos_state.wasm" --run-all-exports > "$out_dir/linked_fpos_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_fpos_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/update_file_state.o" "$out_dir/update_file_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_update_file_state.wasm" \
  "$out_dir/update_file_state.o"
wasm-validate "$out_dir/linked_update_file_state.wasm"
wasm-interp "$out_dir/linked_update_file_state.wasm" --run-all-exports > "$out_dir/linked_update_file_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_update_file_state.interp"

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

"$root/build/ag_c_wasm" -c -o "$out_dir/atexit_state.o" "$out_dir/atexit_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_atexit_state.wasm" \
  "$out_dir/atexit_state.o"
wasm-validate "$out_dir/linked_atexit_state.wasm"
wasm-interp "$out_dir/linked_atexit_state.wasm" --run-all-exports > "$out_dir/linked_atexit_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_atexit_state.interp"

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

"$root/build/ag_c_wasm" -c -o "$out_dir/stdio_size_state.o" "$out_dir/stdio_size_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_stdio_size_state.wasm" \
  "$out_dir/stdio_size_state.o"
wasm-validate "$out_dir/linked_stdio_size_state.wasm"
wasm-interp "$out_dir/linked_stdio_size_state.wasm" --run-all-exports > "$out_dir/linked_stdio_size_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_stdio_size_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/stdio_invalid_state.o" "$out_dir/stdio_invalid_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_stdio_invalid_state.wasm" \
  "$out_dir/stdio_invalid_state.o"
wasm-validate "$out_dir/linked_stdio_invalid_state.wasm"
wasm-interp "$out_dir/linked_stdio_invalid_state.wasm" --run-all-exports > "$out_dir/linked_stdio_invalid_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_stdio_invalid_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/localtime_state.o" "$out_dir/localtime_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_localtime_state.wasm" \
  "$out_dir/localtime_state.o"
wasm-validate "$out_dir/linked_localtime_state.wasm"
wasm-interp "$out_dir/linked_localtime_state.wasm" --run-all-exports > "$out_dir/linked_localtime_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_localtime_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/wide_strto_state.o" "$out_dir/wide_strto_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_wide_strto_state.wasm" \
  "$out_dir/wide_strto_state.o"
wasm-validate "$out_dir/linked_wide_strto_state.wasm"
wasm-interp "$out_dir/linked_wide_strto_state.wasm" --run-all-exports > "$out_dir/linked_wide_strto_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_wide_strto_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/utf8_wide_state.o" "$out_dir/utf8_wide_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_utf8_wide_state.wasm" \
  "$out_dir/utf8_wide_state.o"
wasm-validate "$out_dir/linked_utf8_wide_state.wasm"
wasm-interp "$out_dir/linked_utf8_wide_state.wasm" --run-all-exports > "$out_dir/linked_utf8_wide_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_utf8_wide_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/wide_io_state.o" "$out_dir/wide_io_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_wide_io_state.wasm" \
  "$out_dir/wide_io_state.o"
wasm-validate "$out_dir/linked_wide_io_state.wasm"
wasm-interp "$out_dir/linked_wide_io_state.wasm" --run-all-exports > "$out_dir/linked_wide_io_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_wide_io_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/alloc_state.o" "$out_dir/alloc_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_alloc_state.wasm" \
  "$out_dir/alloc_state.o"
wasm-validate "$out_dir/linked_alloc_state.wasm"
wasm-interp "$out_dir/linked_alloc_state.wasm" --run-all-exports > "$out_dir/linked_alloc_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_alloc_state.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/qsort_size_state.o" "$out_dir/qsort_size_state.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_qsort_size_state.wasm" \
  "$out_dir/qsort_size_state.o"
wasm-validate "$out_dir/linked_qsort_size_state.wasm"
wasm-interp "$out_dir/linked_qsort_size_state.wasm" --run-all-exports > "$out_dir/linked_qsort_size_state.interp"
grep -q 'main() => i32:42' "$out_dir/linked_qsort_size_state.interp"

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
  grep -q '<env.vsprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.vprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.vfprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.scanf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fscanf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.memcpy>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.memmove>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.memchr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strncat>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strstr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strspn>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strcspn>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strpbrk>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strcoll>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strxfrm>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtok>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strerror>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.malloc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.realloc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.aligned_alloc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atof>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atol>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atoll>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtol>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoul>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoll>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoull>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtof>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtod>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtold>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoimax>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strtoumax>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.qsort>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.bsearch>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.rand>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.srand>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.labs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.llabs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.div>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ldiv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lldiv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.imaxdiv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atexit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.at_quick_exit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.exit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.quick_exit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env._Exit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.abort>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getenv>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.realpath>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.system>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.time>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.clock>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.difftime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.timespec_get>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.gmtime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.localtime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mktime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.asctime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ctime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.strftime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsftime>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  grep -q '<env.wcscoll>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsxfrm>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcschr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsrchr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsstr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcsspn>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcscspn>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcspbrk>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstok>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemcpy>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemmove>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemset>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemcmp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wmemchr>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstol>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstoul>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstoll>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstoull>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstof>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstod>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstold>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mblen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbtowc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wctomb>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbstowcs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.wcstombs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbrtowc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbrlen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.mbsinit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  grep -q '<env.fgetwc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getwc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.getwchar>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fputwc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.putwc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.putwchar>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ungetwc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fgetws>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fputws>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fwide>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sscanf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fopen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.freopen>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  grep -q '<env.remainder>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.remainderf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.remainderl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.remquo>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.remquof>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.remquol>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fdim>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fdimf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fdiml>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fma>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmaf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fmal>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.frexp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.frexpf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.frexpl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ldexp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ldexpf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ldexpl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.scalbn>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.scalbnf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.scalbnl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.scalbln>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.scalblnf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.scalblnl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ilogb>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ilogbf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.ilogbl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.logb>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.logbf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.logbl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.modf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.modff>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.modfl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.copysign>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.copysignf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.copysignl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.nan>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.nanf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.nanl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.powf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.powl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sqrtl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cbrtf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cbrtl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fabsl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.exp>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.expf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.expl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.exp2>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.exp2f>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.exp2l>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.expm1>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.expm1f>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.expm1l>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.erf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.erff>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.erfl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.erfc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.erfcf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.erfcl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.logf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.logl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log1p>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log1pf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log1pl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log2>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log2f>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log2l>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log10>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log10f>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.log10l>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.nearbyint>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.nearbyintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.nearbyintl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.rint>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.rintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.rintl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lrint>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lrintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lrintl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.llrint>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.llrintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.llrintl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lround>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lroundf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.lroundl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.llround>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.llroundf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.llroundl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sinh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sinhf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sinhl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.cosh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.coshf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.coshl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tanh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tanhf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tanhl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.asinh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.asinhf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.asinhl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.acosh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.acoshf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.acoshl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atanh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atanhf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.atanhl>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
  grep -q '<env.fpclassify>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isfinite>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isinf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isnan>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isnormal>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.signbit>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isgreater>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isgreaterequal>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isless>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.islessequal>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.islessgreater>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.isunordered>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.sinh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.tanh>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.printf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fprintf>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.puts>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.putchar>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fputs>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.fputc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
  grep -q '<env.putc>' "$out_dir/linked_libc_runtime_nostdlib.objdump"
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
