import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { createToolchain } from "./agc-toolchain.js";
import { inlineStandardIncludes } from "./agc-include-inline.js";

const compilerWasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const linkerWasmPath = process.argv[3] || "build/wasm_linker_selfhost/ag_wasm_link.wasm";
const outDir = "build/wasm_js_pipeline_smoke";
await mkdir(outDir, { recursive: true });

const toolchain = await createToolchain({
  compilerWasm: await readFile(compilerWasmPath),
  linkerWasm: await readFile(linkerWasmPath),
  runtimeObject: await readFile("build/libagc_runtime.o"),
});
const loadInclude = async (name) => readFile(new URL(`../../include/${name}`, import.meta.url), "utf8");

const mainSource = "int other(void); int main(void) { return other() + 1; }\n";
const otherSource = "int other(void) { return 41; }\n";
const mainObj = toolchain.compileObject(mainSource);
const otherObj = toolchain.compileObject(otherSource);

const mainObjPath = path.join(outDir, "main_from_compiler_api.o");
const otherObjPath = path.join(outDir, "other_from_compiler_api.o");
await writeFile(mainObjPath, mainObj);
await writeFile(otherObjPath, otherObj);

try {
  const dump = execFileSync("wasm-objdump", ["-x", mainObjPath], { encoding: "utf8" });
  if (!dump.includes("linking") || !dump.includes("reloc.CODE")) {
    throw new Error("compiler API object is missing linking metadata or call relocation");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

const linked = toolchain.compileLinkedWasm([mainSource, otherSource], {
  exports: ["main"],
  useStdlib: false,
});
if (linked[0] !== 0x00 || linked[1] !== 0x61 || linked[2] !== 0x73 || linked[3] !== 0x6d) {
  throw new Error("pipeline output is not a wasm module");
}

const linkedPath = path.join(outDir, "linked_from_wasm_compiler_and_linker.wasm");
await writeFile(linkedPath, linked);

try {
  execFileSync("wasm-validate", [linkedPath], { stdio: "inherit" });
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

try {
  const out = execFileSync("wasm-interp", [linkedPath, "--run-all-exports"], { encoding: "utf8" });
  if (!out.includes("main() => i32:42")) {
    throw new Error(out.trim() || "wasm-interp produced no main result");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

const instantiated = await toolchain.instantiateLinkedWasm([mainSource, otherSource], {
  exports: ["main"],
  useStdlib: false,
});
if (instantiated.instance.exports.main() !== 42) {
  throw new Error("instantiated pipeline main() did not return 42");
}

const mathSource = `
double sin(double);
double sqrt(double);
double pow(double, double);
double frexp(double, int *);
float frexpf(float, int *);
long double frexpl(long double, int *);
double ldexp(double, int);
float ldexpf(float, int);
long double ldexpl(long double, int);
double modf(double, double *);
float modff(float, float *);
long double modfl(long double, long double *);
double copysign(double, double);
float copysignf(float, float);
long double copysignl(long double, long double);
double nan(char *);
float nanf(char *);
long double nanl(char *);
int fpclassify(double);
int isfinite(double);
int isinf(double);
int isnan(double);
int isnormal(double);
int signbit(double);
int isgreater(double, double);
int isgreaterequal(double, double);
int isless(double, double);
int islessequal(double, double);
int islessgreater(double, double);
int isunordered(double, double);
int main(void) {
  double z = 0.0;
  double nanv = z / z;
  double infv = 1.0 / z;
  double nzero = -z;
  double subnormal = 1.0e-310;
  int expv = 0;
  int fexpv = 0;
  int lexpv = 0;
  double ip = 0.0;
  float fip = 0.0f;
  long double lip = 0.0L;
  if (fpclassify(nanv) != 0 || fpclassify(infv) != 1 || fpclassify(z) != 2) return 1;
  if (fpclassify(subnormal) != 3 || fpclassify(1.0) != 4) return 2;
  if (!isnan(nanv) || isnan(1.0) || !isinf(infv) || isinf(nanv)) return 3;
  if (!isfinite(1.0) || isfinite(infv) || isfinite(nanv)) return 4;
  if (!isnormal(1.0) || isnormal(subnormal)) return 5;
  if (!signbit(-1.0) || !signbit(nzero) || signbit(0.0)) return 6;
  if (!isgreater(2.0, 1.0) || isgreater(nanv, 1.0)) return 7;
  if (!isgreaterequal(2.0, 2.0) || !isless(1.0, 2.0)) return 8;
  if (!islessequal(2.0, 2.0) || !islessgreater(1.0, 2.0)) return 9;
  if (islessgreater(2.0, 2.0) || !isunordered(nanv, 1.0) || isunordered(1.0, 2.0)) return 10;
  if ((int)(frexp(-8.0, &expv) * 1000.0) != -500 || expv != 4) return 11;
  if ((int)(frexpf(4.0f, &fexpv) * 1000.0f) != 500 || fexpv != 3) return 12;
  if ((int)(frexpl(16.0L, &lexpv) * 1000.0L) != 500 || lexpv != 5) return 13;
  if ((int)(ldexp(0.75, 3) * 1000.0) != 6000) return 14;
  if ((int)(ldexpf(0.5f, 4) * 1000.0f) != 8000) return 15;
  if ((int)(ldexpl(0.25L, 5) * 1000.0L) != 8000) return 16;
  if ((int)(modf(-3.75, &ip) * 100.0) != -75 || (int)ip != -3) return 17;
  if ((int)(modff(2.25f, &fip) * 100.0f) != 25 || (int)fip != 2) return 18;
  if ((int)(modfl(5.5L, &lip) * 100.0L) != 50 || (int)lip != 5) return 19;
  if ((int)copysign(2.0, nzero) != -2 || !signbit(copysign(2.0, nzero))) return 20;
  if ((int)copysignf(2.0f, -0.0f) != -2 || !signbit(copysignf(2.0f, -0.0f))) return 21;
  if ((int)copysignl(2.0L, -0.0L) != -2 || !signbit(copysignl(2.0L, -0.0L))) return 22;
  if (!isnan(nan("")) || !isnan(nanf("")) || !isnan(nanl(""))) return 23;
  return (int)(sin(1.5707963267948966) * 1000.0) + (int)sqrt(4.0) + (int)pow(2.0, 3.0);
}
`;
const mathLinked = toolchain.compileLinkedWasm(mathSource, { exports: ["main"], useStdlib: false });
const mathLinkedPath = path.join(outDir, "linked_math_imports_from_api.wasm");
await writeFile(mathLinkedPath, mathLinked);
try {
  const dump = execFileSync("wasm-objdump", ["-x", mathLinkedPath], { encoding: "utf8" });
  if (!dump.includes("env.sin") ||
      !dump.includes("env.sqrt") ||
      !dump.includes("env.pow")) {
    throw new Error("linked math wasm did not import JS math helpers");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}
const mathInstantiated = await toolchain.instantiateLinkedWasm(mathSource, {
  exports: ["main"],
  useStdlib: false,
});
if (mathInstantiated.instance.exports.main() !== 1010) {
  throw new Error("instantiated math pipeline did not use JS math imports");
}

const linkedStdioSource = await inlineStandardIncludes(`#include <stdio.h>
#include <stdarg.h>
static int call_vprintf(const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vprintf(fmt, ap);
  va_end(ap);
  return n;
}
int main(void) {
  printf("%*s:%03d:aa", 4, "x", 7);
  if (call_vprintf(":v%d", 8) != 3) return 9;
  perror("runtime");
  return 7;
}
`, { loadInclude });
let linkedStdioStdout = "";
let linkedStdioStderr = "";
const linkedStdio = await toolchain.instantiateLinkedWasm(linkedStdioSource, {
  exports: [
    "main",
    "__agc_runtime_stdout_ptr",
    "__agc_runtime_stdout_len",
    "__agc_runtime_stderr_ptr",
    "__agc_runtime_stderr_len",
  ],
  useStdlib: true,
}, {
  onStdout: (chunk) => { linkedStdioStdout += chunk; },
  onStderr: (chunk) => { linkedStdioStderr += chunk; },
});
if (linkedStdio.instance.exports.main() !== 7) {
  throw new Error("instantiated stdio import pipeline did not use JS stdio imports");
}
if (linkedStdioStdout === "") linkedStdioStdout = linkedStdio.readStdout();
if (linkedStdioStdout !== "   x:007:aa:v8") {
  throw new Error(`instantiated stdio import pipeline stdout mismatch: ${JSON.stringify(linkedStdioStdout)}`);
}
if (linkedStdioStderr === "") linkedStdioStderr = linkedStdio.readStderr();
if (linkedStdioStderr !== "runtime: error\n") {
  throw new Error(`instantiated stdio import pipeline stderr mismatch: ${JSON.stringify(linkedStdioStderr)}`);
}

const linkedStdioErrorSource = await inlineStandardIncludes(`#include <stdio.h>
static int same_text(char *a, char *b) {
  int i = 0;
  while (a[i] && b[i] && a[i] == b[i]) i++;
  return a[i] == b[i];
}
static int call_vsprintf(char *buf, const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsprintf(buf, fmt, ap);
  va_end(ap);
  return n;
}
int main(void) {
  char b[1];
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 1;
  if (fwrite("A", 1, 1, wf) != 1) return 2;
  if (fseek(wf, 0, SEEK_SET) != 0) return 3;
  if (fread(b, 1, 1, wf) != 0) return 4;
  if (!ferror(wf)) return 5;
  clearerr(wf);
  if (ferror(wf)) return 6;
  if (fclose(wf) != 0) return 7;

  FILE *rf = fopen("tmp.txt", "r");
  if (!rf) return 8;
  if (fwrite("B", 1, 1, rf) != 0) return 9;
  if (fputs("B", rf) != EOF) return 10;
  if (fputc('B', rf) != EOF) return 11;
  if (!ferror(rf)) return 12;
  clearerr(rf);
  if (ferror(rf)) return 13;
  if (fclose(rf) != 0) return 14;
  if (remove(NULL) == 0) return 15;
  if (remove("tmp.txt") != 0) return 16;
  rf = fopen("tmp.txt", "r");
  if (!rf) return 17;
  if (fgetc(rf) != EOF || !feof(rf)) return 18;
  if (fclose(rf) != 0) return 19;

  FILE *uf = fopen("tmp.txt", "w");
  if (!uf) return 20;
  if (fputc('A', uf) != 'A' || fputc('B', uf) != 'B') return 21;
  if (fclose(uf) != 0) return 22;
  uf = fopen("tmp.txt", "r");
  if (!uf) return 23;
  if (fgetc(uf) != 'A') return 24;
  if (ungetc('Q', uf) != 'Q') return 25;
  if (fgetc(uf) != 'Q') return 26;
  if (fgetc(uf) != 'B') return 27;
  if (fclose(uf) != 0) return 28;
  if (rename(NULL, "new.txt") == 0) return 29;
  if (rename("tmp.txt", NULL) == 0) return 30;
  if (rename("tmp.txt", "new.txt") != 0) return 31;
  uf = fopen("new.txt", "r");
  if (!uf) return 32;
  if (fgetc(uf) != 'A') return 33;
  if (fclose(uf) != 0) return 34;
  char iobuf[BUFSIZ];
  if (setvbuf(stdout, NULL, _IONBF, 0) != 0) return 35;
  if (setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf)) != 0) return 36;
  if (setvbuf(stdout, iobuf, 99, sizeof(iobuf)) == 0) return 37;

  uf = fopen("tmp.txt", "w");
  if (!uf) return 38;
  if (setvbuf(uf, iobuf, _IOFBF, sizeof(iobuf)) != 0) return 39;
  setbuf(uf, NULL);
  if (fputc('1', uf) != '1' || fputc('2', uf) != '2' ||
      fputc(' ', uf) != ' ' || fputc('3', uf) != '3' ||
      fputc('4', uf) != '4') return 40;
  if (fclose(uf) != 0) return 41;
  uf = fopen("tmp.txt", "r");
  if (!uf) return 43;
  setbuf(uf, iobuf);
  if (fgetc(uf) != '1') return 44;
  fpos_t pos;
  if (fgetpos(uf, &pos) != 0) return 49;
  if (fgetc(uf) != '2') return 50;
  if (fsetpos(uf, &pos) != 0) return 51;
  if (fgetc(uf) != '2') return 52;
  if (ungetc('8', uf) != '8') return 53;
  if (fsetpos(uf, &pos) != 0) return 54;
  if (fgetc(uf) != '2') return 55;
  if (fgetpos(uf, NULL) == 0) return 56;
  if (fsetpos(uf, NULL) == 0) return 57;
  if (fsetpos(uf, &pos) != 0) return 58;
  if (ungetc('9', uf) != '9') return 45;
  int scan_a = 0;
  int scan_b = 0;
  int scan_n = 0;
  if (fscanf(uf, "%d %d%n", &scan_a, &scan_b, &scan_n) != 2) return 46;
  if (scan_a != 92 || scan_b != 34 || scan_n != 5) return 47;
  if (fclose(uf) != 0) return 48;

  FILE *up = fopen("tmp.txt", "w+");
  if (!up) return 59;
  if (fputc('A', up) != 'A' || fputc('B', up) != 'B') return 60;
  if (fseek(up, 0, SEEK_SET) != 0) return 61;
  if (fgetc(up) != 'A') return 62;
  if (fseek(up, 1, SEEK_SET) != 0) return 63;
  if (fputc('Z', up) != 'Z') return 64;
  if (fseek(up, 0, SEEK_SET) != 0) return 65;
  if (fgetc(up) != 'A' || fgetc(up) != 'Z' || fgetc(up) != EOF) return 66;
  if (fclose(up) != 0) return 67;

  FILE *tp = tmpfile();
  if (!tp) return 68;
  if (fputc('T', tp) != 'T') return 69;
  if (fseek(tp, 0, SEEK_SET) != 0) return 70;
  if (fgetc(tp) != 'T' || fgetc(tp) != EOF) return 71;
  if (fclose(tp) != 0) return 72;

  char tname1[L_tmpnam];
  char tname2[L_tmpnam];
  if (TMP_MAX < 2) return 73;
  if (tmpnam(tname1) != tname1 || tname1[0] == 0) return 74;
  char *static_name = tmpnam(NULL);
  if (!static_name || static_name[0] == 0) return 75;
  if (tmpnam(tname2) != tname2 || tname2[0] == 0) return 76;
  if (same_text(tname1, tname2)) return 77;
  FILE *tn = fopen(tname1, "w+");
  if (!tn) return 78;
  if (fputc('N', tn) != 'N') return 79;
  if (fseek(tn, 0, SEEK_SET) != 0) return 80;
  if (fgetc(tn) != 'N') return 81;
  if (fclose(tn) != 0) return 82;

  char vsbuf[16];
  if (call_vsprintf(vsbuf, "%d-%s", 12, "ok") != 5) return 83;
  if (vsbuf[0] != '1' || vsbuf[1] != '2' || vsbuf[2] != '-' ||
      vsbuf[3] != 'o' || vsbuf[4] != 'k' || vsbuf[5] != 0) return 84;
  FILE *fr = fopen("tmp.txt", "r");
  if (!fr) return 85;
  if (freopen("tmp.txt", "w+", fr) != fr) return 86;
  if (fputc('R', fr) != 'R') return 87;
  if (putc('S', fr) != 'S') return 88;
  if (fseek(fr, 0, SEEK_SET) != 0) return 89;
  if (fgetc(fr) != 'R') return 90;
  if (fgetc(fr) != 'S') return 91;
  if (fgetc(fr) != EOF || !feof(fr)) return 92;
  if (fclose(fr) != 0) return 93;
  return 42;
}
`, { loadInclude });
const linkedStdioError = await toolchain.instantiateLinkedWasm(linkedStdioErrorSource, {
  exports: ["main"],
  useStdlib: true,
});
const linkedStdioErrorResult = linkedStdioError.instance.exports.main();
if (linkedStdioErrorResult !== 42) {
  throw new Error(`linked runtime stdio error state failed: ${linkedStdioErrorResult}`);
}

const linkedStdlibConversionSource = await inlineStandardIncludes(`#include <stdlib.h>
#include <errno.h>
int main(void) {
  char *fend = 0;
  char *dend = 0;
  char *ldend = 0;
  char *llend = 0;
  char *ullend = 0;
  char *ovend = 0;
  char *uovend = 0;
  char *unegend = 0;
  char *dovend = 0;
  char *dunderend = 0;
  char *dzeroend = 0;
  char *dinfend = 0;
  char *dninfend = 0;
  char *dnanend = 0;
  float f = strtof(" .5!", &fend);
  double d = strtod(" -12.5e1!", &dend);
  long double ld = strtold("0x1.8p+3!", &ldend);
  long long ll = strtoll(" -80000000!", &llend, 16);
  unsigned long long ull = strtoull("ffffffff!", &ullend, 16);
  int wc = 0;
  int wide[4];
  char mb[8];
  char *a16 = aligned_alloc(16, 32);
  char *a32 = aligned_alloc(32, 0);
  if ((int)(f * 100.0f) != 50 || *fend != '!') return 1;
  if ((int)d != -125 || *dend != '!') return 2;
  if ((int)ld != 12 || *ldend != '!') return 3;
  if (mblen("Q", 2) != 1 || mbtowc(&wc, "Q", 2) != 1 || wc != 'Q') return 4;
  if (wctomb(mb, 'R') != 1 || mb[0] != 'R') return 5;
  if (mbstowcs(wide, "Hi", 4) != 2 || wide[0] != 'H' || wide[1] != 'i') return 6;
  if (wcstombs(mb, wide, sizeof(mb)) != 2 || mb[0] != 'H' || mb[1] != 'i') return 7;
  if (atoll(" -1234567890123x") != -1234567890123LL) return 8;
  if (ll != -2147483648LL || *llend != '!') return 9;
  if (ull != 4294967295ULL || *ullend != '!') return 10;
  if (llabs(-1234567890123LL) != 1234567890123LL) return 11;
  errno = 0;
  if (strtoll("9223372036854775808!", &ovend, 10) != 9223372036854775807LL ||
      *ovend != '!' || errno != ERANGE) return 12;
  errno = 0;
  if (strtoull("18446744073709551616!", &uovend, 10) != ~0ULL ||
      *uovend != '!' || errno != ERANGE) return 13;
  errno = 0;
  if (strtoull("-1!", &unegend, 10) != ~0ULL || *unegend != '!' || errno != 0) return 14;
  errno = 0;
  if (!(strtod("1e309!", &dovend) > 1e300) || *dovend != '!' || errno != ERANGE) return 15;
  errno = 0;
  if (strtod("1e-400!", &dunderend) != 0.0 || *dunderend != '!' || errno != ERANGE) return 16;
  errno = 0;
  if (strtod("0e9999!", &dzeroend) != 0.0 || *dzeroend != '!' || errno != 0) return 17;
  errno = 0;
  if (!(strtod("INF!", &dinfend) > 1e300) || *dinfend != '!' || errno != 0) return 18;
  errno = 0;
  if (!(strtof("-infinity!", &dninfend) < -1e30f) || *dninfend != '!' || errno != 0) return 19;
  errno = 0;
  long double nanv = strtold("nan(payload)!", &dnanend);
  if (!(nanv != 0.0L && !(nanv < 0.0L) && !(nanv > 0.0L)) || *dnanend != '!' || errno != 0) return 20;
  div_t divv = div(-7, 3);
  ldiv_t ldivv = ldiv(-7000000000L, 3000000000L);
  lldiv_t lldivv = lldiv(7000000000000LL, -3000000000000LL);
  if (divv.quot != -2 || divv.rem != -1) return 21;
  if (ldivv.quot != -2 || ldivv.rem != -1000000000L) return 22;
  if (lldivv.quot != -2 || lldivv.rem != 1000000000000LL) return 23;
  if (!a16 || !a32 || ((long)a16 % 16) != 0 || ((long)a32 % 32) != 0) return 24;
  a16[0] = 'A';
  a16[31] = 'Z';
  if (a16[0] != 'A' || a16[31] != 'Z') return 25;
  if (aligned_alloc(12, 24) != 0 || aligned_alloc(16, 24) != 0) return 26;
  return 42;
}
`, { loadInclude });
const linkedStdlibConversion = await toolchain.instantiateLinkedWasm(linkedStdlibConversionSource, {
  exports: ["main"],
  useStdlib: true,
});
const linkedStdlibConversionResult = linkedStdlibConversion.instance.exports.main();
if (linkedStdlibConversionResult !== 42) {
  throw new Error(`linked runtime stdlib conversion failed: ${linkedStdlibConversionResult}`);
}

const linkedTimeSource = await inlineStandardIncludes(`#include <time.h>
#include <string.h>
#include <wchar.h>
int main(void) {
  time_t epoch = 0;
  time_t sample = 90061;
  struct tm *epoch_tm = localtime(&epoch);
  int epoch_ok = epoch_tm && epoch_tm->tm_year == 70 && epoch_tm->tm_mon == 0 &&
                 epoch_tm->tm_mday == 1 && epoch_tm->tm_wday == 4;
  struct tm *tm = gmtime(&sample);
  char buf[64];
  wchar_t wbuf[64];
  wchar_t wfmt[] = {'%', 'F', ' ', '%', 'T', 0};
  size_t n;
  size_t wn;
  struct tm mk = {0};
  struct timespec ts = {-1, -1};
  if (!epoch_ok) return 1;
  if (!tm || tm->tm_sec != 1 || tm->tm_min != 1 || tm->tm_hour != 1 ||
      tm->tm_mday != 2 || tm->tm_wday != 5 || tm->tm_yday != 1) return 2;
  if (timespec_get(&ts, TIME_UTC) != TIME_UTC || ts.tv_sec != 0 || ts.tv_nsec != 0) return 10;
  if (timespec_get(&ts, 99) != 0) return 11;
  if (strcmp(asctime(tm), "Fri Jan  2 01:01:01 1970\\n") != 0) return 3;
  if (strcmp(ctime(&sample), "Fri Jan  2 01:01:01 1970\\n") != 0) return 4;
  n = strftime(buf, sizeof(buf), "%F %T %a %b %j", tm);
  if (n != 31 || strcmp(buf, "1970-01-02 01:01:01 Fri Jan 002") != 0) return 5;
  if (strftime(buf, 8, "%Y-%m-%d", tm) != 0) return 6;
  wn = wcsftime(wbuf, 64, wfmt, tm);
  if (wn != 19 || wbuf[0] != '1' || wbuf[3] != '0' || wbuf[4] != '-' ||
      wbuf[10] != ' ' || wbuf[18] != '1' || wbuf[19] != 0) return 7;
  if (wcsftime(wbuf, 8, wfmt, tm) != 0) return 8;
  mk.tm_sec = 0;
  mk.tm_min = 0;
  mk.tm_hour = 0;
  mk.tm_mday = 3;
  mk.tm_mon = 0;
  mk.tm_year = 70;
  mk.tm_wday = 0;
  mk.tm_yday = 0;
  mk.tm_isdst = -1;
  if (mktime(&mk) != 172800 || mk.tm_wday != 6 || mk.tm_yday != 2 || mk.tm_isdst != 0) return 9;
  return 42;
}
`, { loadInclude });
const linkedTime = await toolchain.instantiateLinkedWasm(linkedTimeSource, {
  exports: ["main"],
  useStdlib: true,
});
const linkedTimeResult = linkedTime.instance.exports.main();
if (linkedTimeResult !== 42) {
  throw new Error(`linked runtime time.h failed: ${linkedTimeResult}`);
}

const linkedStringLocaleSource = await inlineStandardIncludes(`#include <string.h>
int main(void) {
  char buf[8];
  unsigned long n = strxfrm(buf, "hello", sizeof(buf));
  unsigned long need = strxfrm(buf, "abcdef", 0);
  if (strcoll("abc", "abd") >= 0) return 1;
  if (strcoll("same", "same") != 0) return 2;
  if (n != 5 || strcmp(buf, "hello") != 0) return 3;
  if (need != 6) return 4;
  return 42;
}
`, { loadInclude });
const linkedStringLocale = await toolchain.instantiateLinkedWasm(linkedStringLocaleSource, {
  exports: ["main"],
  useStdlib: true,
});
const linkedStringLocaleResult = linkedStringLocale.instance.exports.main();
if (linkedStringLocaleResult !== 42) {
  throw new Error(`linked runtime string collation failed: ${linkedStringLocaleResult}`);
}

const linkedWideConversionSource = await inlineStandardIncludes(`#include <wchar.h>
#include <errno.h>
int main(void) {
  wchar_t llhex[] = {'-', '8', '0', '0', '0', '0', '0', '0', '0', '!', 0};
  wchar_t ullhex[] = {'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', '!', 0};
  wchar_t expf[] = {'-', '1', '.', '2', '5', 'e', '2', '!', 0};
  wchar_t hexfloat[] = {'0', 'x', '1', '.', '8', 'p', '+', '3', '!', 0};
  wchar_t infv[] = {'i', 'n', 'f', '!', 0};
  wchar_t ov[] = {'9', '2', '2', '3', '3', '7', '2', '0', '3', '6', '8', '5', '4', '7', '7', '5', '8', '0', '8', '!', 0};
  wchar_t *end = 0;
  wchar_t s[] = {'a', 'b', 'a', 'c', 'd', 0};
  wchar_t accept[] = {'a', 'b', 0};
  wchar_t reject[] = {'c', 0};
  wchar_t pbrk[] = {'c', 'd', 0};
  wchar_t dst[8];
  wchar_t tok[] = {'a', 'a', ',', 'b', 'b', ',', ',', 'c', 'c', 0};
  wchar_t delim[] = {',', 0};
  wchar_t *save = 0;
  wchar_t *t1;
  wchar_t *t2;
  wchar_t *t3;
  wchar_t *t4;
  if (wcstoll(llhex, &end, 16) != -2147483648LL || *end != '!') return 1;
  if (wcstoull(ullhex, &end, 16) != 4294967295ULL || *end != '!') return 2;
  if ((int)wcstof(expf, &end) != -125 || *end != '!') return 3;
  if ((int)wcstold(hexfloat, &end) != 12 || *end != '!') return 4;
  errno = 0;
  if (!(wcstod(infv, &end) > 1e300) || *end != '!' || errno != 0) return 5;
  errno = 0;
  if (wcstoll(ov, &end, 10) != 9223372036854775807LL || *end != '!' || errno != ERANGE) return 6;
  t1 = wcstok(tok, delim, &save);
  t2 = wcstok(0, delim, &save);
  t3 = wcstok(0, delim, &save);
  t4 = wcstok(0, delim, &save);
  if (wcscoll(s, accept) <= 0) return 7;
  if (wcsxfrm(dst, accept, 8) != 2 || dst[0] != 'a' || dst[1] != 'b' || dst[2] != 0) return 8;
  if (wcsxfrm(0, s, 0) != 5) return 9;
  if (wcsspn(s, accept) != 3) return 10;
  if (wcscspn(s, reject) != 3) return 11;
  if (wcspbrk(s, pbrk) != s + 3) return 12;
  if (t1 != tok || t1[0] != 'a' || t1[1] != 'a' || t1[2] != 0) return 13;
  if (t2[0] != 'b' || t2[1] != 'b' || t2[2] != 0) return 14;
  if (t3[0] != 'c' || t3[1] != 'c' || t3[2] != 0 || t4 != 0) return 15;
  if (mbrlen("A", 2, 0) != 1 || mbrlen("", 1, 0) != 0 || !mbsinit(0)) return 16;
  return 42;
}
`, { loadInclude });
const linkedWideConversion = await toolchain.instantiateLinkedWasm(linkedWideConversionSource, {
  exports: ["main"],
  useStdlib: true,
});
const linkedWideConversionResult = linkedWideConversion.instance.exports.main();
if (linkedWideConversionResult !== 42) {
  throw new Error(`linked runtime wide conversion failed: ${linkedWideConversionResult}`);
}

const linkedWideIOSource = await inlineStandardIncludes(`#include <stdio.h>
#include <wchar.h>
int main(void) {
  FILE *fp = fopen("wide.txt", "w+");
  wchar_t line[8];
  wchar_t text[] = {'O', 0x3042, '\\n', 0};
  if (!fp) return 1;
  if (fwide(fp, 1) != 1 || fwide(fp, -1) != -1 || fwide(fp, 0) != 0) return 2;
  if (fputwc('A', fp) != 'A') return 3;
  if (putwc(0x3042, fp) != 0x3042) return 4;
  if (fputwc('\\n', fp) != '\\n') return 5;
  if (fputws(text, fp) != 3) return 6;
  rewind(fp);
  if (fgetwc(fp) != 'A') return 7;
  if (ungetwc('Z', fp) != 'Z') return 8;
  if (getwc(fp) != 'Z') return 9;
  if (fgetwc(fp) != 0x3042) return 10;
  if (fgetwc(fp) != '\\n') return 11;
  if (fgetws(line, 8, fp) != line) return 12;
  if (line[0] != 'O' || line[1] != 0x3042 || line[2] != '\\n' || line[3] != 0) return 13;
  if (fgetwc(fp) != WEOF) return 14;
  if (ungetwc(0x3042, fp) != WEOF) return 15;
  if (fclose(fp) != 0) return 16;
  if (putwchar('Q') != 'Q') return 17;
  return 42;
}
`, { loadInclude });
const linkedWideIO = await toolchain.instantiateLinkedWasm(linkedWideIOSource, {
  exports: ["main", "__agc_runtime_stdout_ptr", "__agc_runtime_stdout_len"],
  useStdlib: true,
});
const linkedWideIOResult = linkedWideIO.instance.exports.main();
if (linkedWideIOResult !== 42) {
  throw new Error(`linked runtime wide I/O failed: ${linkedWideIOResult}`);
}
if (linkedWideIO.readStdout() !== "Q") {
  throw new Error(`linked runtime putwchar stdout mismatch: ${JSON.stringify(linkedWideIO.readStdout())}`);
}

const repeatedWideHeaderASource = await inlineStandardIncludes(`#include <wchar.h>
int wide_header_repeat_a(void) {
  return sizeof(mbstate_t) > 0;
}
`, { loadInclude });
const repeatedWideHeaderBSource = await inlineStandardIncludes(`#include <wchar.h>
int wide_header_repeat_b(void) {
  return sizeof(mbstate_t) > 0;
}
`, { loadInclude });
const repeatedWideHeaderLinked = toolchain.compileLinkedWasm([
  repeatedWideHeaderASource,
  repeatedWideHeaderBSource,
], {
  exports: ["wide_header_repeat_a", "wide_header_repeat_b"],
  useStdlib: false,
});
const repeatedWideHeaderModule = await WebAssembly.instantiate(repeatedWideHeaderLinked, {});
if (repeatedWideHeaderModule.instance.exports.wide_header_repeat_a() !== 1 ||
    repeatedWideHeaderModule.instance.exports.wide_header_repeat_b() !== 1) {
  throw new Error("compiler API did not reset typedef state between repeated wchar.h sources");
}

const linkedAtexitExitSource = `
int atexit(void (*func)(void));
void exit(int status);
int printf(const char *fmt, ...);
void first(void) { printf("A"); }
void second(void) { printf("B"); }
int main(void) {
  if (atexit(first) != 0) return 1;
  if (atexit(second) != 0) return 2;
  exit(9);
  return 3;
}
`;
const linkedAtexitExit = await toolchain.instantiateLinkedWasm(linkedAtexitExitSource, {
  exports: [
    "main",
    "__agc_runtime_stdout_ptr",
    "__agc_runtime_stdout_len",
    "__agc_runtime_termination_kind",
    "__agc_runtime_termination_status",
  ],
  useStdlib: true,
});
let linkedAtexitTrapped = false;
try {
  linkedAtexitExit.instance.exports.main();
} catch (_) {
  linkedAtexitTrapped = true;
}
if (!linkedAtexitTrapped) {
  throw new Error("linked runtime exit() did not trap after termination notification");
}
if (linkedAtexitExit.readStdout() !== "BA") {
  throw new Error(`linked runtime atexit handlers did not run in reverse order: ${JSON.stringify(linkedAtexitExit.readStdout())}`);
}
if (Number(linkedAtexitExit.instance.exports.__agc_runtime_termination_kind()) !== 1 ||
    Number(linkedAtexitExit.instance.exports.__agc_runtime_termination_status()) !== 9) {
  throw new Error("linked runtime exit() did not preserve termination kind/status after atexit");
}

const linkedQuickExitSource = `
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
void quick_exit(int status);
int printf(const char *fmt, ...);
void normal(void) { printf("X"); }
void first(void) { printf("A"); }
void second(void) { printf("B"); }
int main(void) {
  if (atexit(normal) != 0) return 1;
  if (at_quick_exit(first) != 0) return 2;
  if (at_quick_exit(second) != 0) return 3;
  quick_exit(7);
  return 4;
}
`;
const linkedQuickExit = await toolchain.instantiateLinkedWasm(linkedQuickExitSource, {
  exports: [
    "main",
    "__agc_runtime_stdout_ptr",
    "__agc_runtime_stdout_len",
    "__agc_runtime_termination_kind",
    "__agc_runtime_termination_status",
  ],
  useStdlib: true,
});
let linkedQuickExitTrapped = false;
try {
  linkedQuickExit.instance.exports.main();
} catch (_) {
  linkedQuickExitTrapped = true;
}
if (!linkedQuickExitTrapped) {
  throw new Error("linked runtime quick_exit() did not trap after termination notification");
}
if (linkedQuickExit.readStdout() !== "BA") {
  throw new Error(`linked runtime quick_exit handlers did not run without atexit handlers: ${JSON.stringify(linkedQuickExit.readStdout())}`);
}
if (Number(linkedQuickExit.instance.exports.__agc_runtime_termination_kind()) !== 3 ||
    Number(linkedQuickExit.instance.exports.__agc_runtime_termination_status()) !== 7) {
  throw new Error("linked runtime quick_exit() did not preserve termination kind/status");
}

const linkedUnderscoreExitSource = `
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
void _Exit(int status);
int printf(const char *fmt, ...);
void normal(void) { printf("X"); }
void quick(void) { printf("Q"); }
int main(void) {
  if (atexit(normal) != 0) return 1;
  if (at_quick_exit(quick) != 0) return 2;
  _Exit(5);
  return 3;
}
`;
const linkedUnderscoreExit = await toolchain.instantiateLinkedWasm(linkedUnderscoreExitSource, {
  exports: [
    "main",
    "__agc_runtime_stdout_ptr",
    "__agc_runtime_stdout_len",
    "__agc_runtime_termination_kind",
    "__agc_runtime_termination_status",
  ],
  useStdlib: true,
});
let linkedUnderscoreExitTrapped = false;
try {
  linkedUnderscoreExit.instance.exports.main();
} catch (_) {
  linkedUnderscoreExitTrapped = true;
}
if (!linkedUnderscoreExitTrapped) {
  throw new Error("linked runtime _Exit() did not trap after termination notification");
}
if (linkedUnderscoreExit.readStdout() !== "") {
  throw new Error(`linked runtime _Exit() unexpectedly ran handlers: ${JSON.stringify(linkedUnderscoreExit.readStdout())}`);
}
if (Number(linkedUnderscoreExit.instance.exports.__agc_runtime_termination_kind()) !== 4 ||
    Number(linkedUnderscoreExit.instance.exports.__agc_runtime_termination_status()) !== 5) {
  throw new Error("linked runtime _Exit() did not preserve termination kind/status");
}

const linkedStdinSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) {
  char buf[4];
  int a = getchar();
  char *p = fgets(buf, sizeof(buf), stdin);
  int z = fgetc(stdin);
  int eof = fgetc(stdin);
  if (a != 'A') return 1;
  if (!p || buf[0] != 'B' || buf[1] != 'C' || buf[2] != '\\n' || buf[3] != 0) return 2;
  if (z != 'Z') return 3;
  if (eof != EOF || !feof(stdin)) return 4;
  return 42;
}
`, { loadInclude });
const linkedStdin = await toolchain.instantiateLinkedWasm(linkedStdinSource, {
  exports: ["main"],
  useStdlib: true,
}, {
  stdio: { stdin: "ABC\nZ" },
});
if (linkedStdin.instance.exports.main() !== 42) {
  throw new Error("linked runtime stdin injection failed");
}

const linkedLargeStdinSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) {
  char buf[600];
  unsigned long n = fread(buf, 1, sizeof(buf), stdin);
  unsigned long i = 0;
  int sum = 0;
  if (n != sizeof(buf)) return 1;
  while (i < sizeof(buf)) {
    sum += buf[i];
    i++;
  }
  return sum == 600 * 'x' ? 42 : 2;
}
`, { loadInclude });
const linkedLargeStdin = await toolchain.instantiateLinkedWasm(linkedLargeStdinSource, {
  exports: ["main"],
  useStdlib: true,
}, {
  stdio: { stdin: "x".repeat(600) },
});
if (linkedLargeStdin.instance.exports.main() !== 42) {
  throw new Error("linked runtime large stdin injection failed");
}

const linkedGetlineSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) {
  char *line = 0;
  unsigned long cap = 0;
  int first = getchar();
  if (first != 'f') return 1;
  if (ungetc('F', stdin) != 'F') return 2;
  long n1 = getline(&line, &cap, stdin);
  if (n1 != 6 || line[0] != 'F' || line[4] != 't' || line[5] != '\\n' || line[6] != 0) return 3;
  long n2 = getline(&line, &cap, stdin);
  if (n2 != 7 || line[0] != 's' || line[6] != '\\n' || line[7] != 0) return 4;
  long n3 = getline(&line, &cap, stdin);
  if (n3 != -1 || !feof(stdin)) return 5;
  if (cap < 8) return 6;
  return 42;
}
`, { loadInclude });
const linkedGetline = await toolchain.instantiateLinkedWasm(linkedGetlineSource, {
  exports: ["main"],
  useStdlib: true,
}, {
  stdio: { stdin: "first\nsecond\n" },
});
const linkedGetlineResult = linkedGetline.instance.exports.main();
if (linkedGetlineResult !== 42) {
  throw new Error(`linked runtime getline stdin failed: ${linkedGetlineResult}`);
}

const linkedScanfSource = await inlineStandardIncludes(`#include <stdio.h>
#include <stdarg.h>
static int call_vsscanf(const char *src, const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsscanf(src, fmt, ap);
  va_end(ap);
  return n;
}
static int call_vscanf(const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vscanf(fmt, ap);
  va_end(ap);
  return n;
}
static int call_vfscanf(FILE *stream, const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vfscanf(stream, fmt, ap);
  va_end(ap);
  return n;
}
int main(void) {
  int a = 0;
  unsigned int x = 0;
  char s[5];
  char c = 0;
  int n = 0;
  int r = sscanf(" -42 2a abcZ", "%d %x %3s%c%n", &a, &x, s, &c, &n);
  if (r != 4) return 1;
  if (a != -42 || x != 42) return 2;
  if (s[0] != 'a' || s[1] != 'b' || s[2] != 'c' || s[3] != 0) return 3;
  if (c != 'Z' || n != 12) return 4;
  a = 0;
  x = 0;
  s[0] = s[1] = s[2] = s[3] = s[4] = 0;
  c = 0;
  n = 0;
  r = call_vsscanf(" -11 1f uvwR", "%d %x %3s%c%n", &a, &x, s, &c, &n);
  if (r != 4 || a != -11 || x != 31) return 35;
  if (s[0] != 'u' || s[1] != 'v' || s[2] != 'w' || s[3] != 0) return 36;
  if (c != 'R' || n != 12) return 37;
  signed char hh = 0;
  unsigned char uhh = 0;
  r = sscanf("-5 250", "%hhd %hhu", &hh, &uhh);
  if (r != 2 || hh != -5 || uhh != 250) return 13;
  int na = 0;
  int nb = 0;
  int nc = 0;
  signed char nhh = 0;
  short nh = 0;
  long nl = 0;
  r = sscanf("12 345 6", "%d%hhn %d%hn %d%ln", &na, &nhh, &nb, &nh, &nc, &nl);
  if (r != 3 || na != 12 || nhh != 2 || nb != 345 || nh != 6 || nc != 6 || nl != 8) return 14;
  int ws[3];
  int wc[1];
  r = sscanf("hi Z", "%ls %lc", ws, wc);
  if (r != 2 || ws[0] != 'h' || ws[1] != 'i' || ws[2] != 0 || wc[0] != 'Z') return 15;
  void *p = 0;
  r = sscanf("0x2a", "%p", &p);
  if (r != 1 || (long)p != 42) return 16;
  char set[4];
  char notz[4];
  r = sscanf("abc123Z", "%3[a-z]%3[^Z]", set, notz);
  if (r != 2) return 17;
  if (set[0] != 'a' || set[1] != 'b' || set[2] != 'c' || set[3] != 0) return 18;
  if (notz[0] != '1' || notz[1] != '2' || notz[2] != '3' || notz[3] != 0) return 19;
  float sf = 0.0f;
  double sd = 0.0;
  long double sld = 0.0;
  r = sscanf("1.25 -2.5e1 0x1.8p+3!", "%f %lf %Lf", &sf, &sd, &sld);
  if (r != 3) return 20;
  if ((int)(sf * 100.0f) != 125 || (int)sd != -25 || (int)sld != 12) return 21;
  r = sscanf("12.34x", "%4f", &sf);
  if (r != 1 || (int)(sf * 10.0f) != 123) return 22;
  sf = 0.0f;
  sd = 0.0;
  sld = 0.0;
  r = sscanf("nan(payload) INF -infinity", "%f %lf %Lf", &sf, &sd, &sld);
  if (r != 3) return 23;
  if (!(sf != 0.0f && !(sf < 0.0f) && !(sf > 0.0f))) return 24;
  if (!(sd > 1000000.0 && sld < -1000000.0L)) return 25;
  int fn = 0;
  r = sscanf("infinity", "%3lf%n", &sd, &fn);
  if (r != 1 || !(sd > 1000000.0) || fn != 3) return 26;
  r = sscanf("12,5", "%lf%n", &sd, &fn);
  if (r != 1 || (int)sd != 12 || fn != 2) return 34;
  r = sscanf("", "");
  if (r != 0) return 27;
  fn = 7;
  r = sscanf("", "%n", &fn);
  if (r != 0 || fn != 0) return 28;
  a = 99;
  r = sscanf("", "%d", &a);
  if (r != -1 || a != 99) return 29;
  r = sscanf("x", "%d", &a);
  if (r != 0 || a != 99) return 30;

  a = 0;
  x = 0;
  s[0] = s[1] = s[2] = s[3] = s[4] = 0;
  c = 0;
  n = 0;
  r = call_vscanf("%d %x %2s%c%n", &a, &x, s, &c, &n);
  if (r != 4) return 38;
  if (a != 66 || x != 43) return 39;
  if (s[0] != 'y' || s[1] != 'o' || s[2] != 0) return 40;
  if (c != 'Q' || n != 9) return 41;

  a = 0;
  x = 0;
  s[0] = s[1] = s[2] = s[3] = s[4] = 0;
  c = 0;
  n = 0;
  r = scanf("%d %x %2s%c%n", &a, &x, s, &c, &n);
  if (r != 4) return 5;
  if (a != 55 || x != 42) return 6;
  if (s[0] != 'o' || s[1] != 'k' || s[2] != 0) return 7;
  if (c != 'Z' || n != 9) return 8;

  a = 0;
  s[0] = s[1] = s[2] = s[3] = s[4] = 0;
  n = 0;
  r = call_vfscanf(stdin, "%d %4s%n", &a, s, &n);
  if (r != 2) return 9;
  if (a != 17) return 10;
  if (s[0] != 'd' || s[1] != 'o' || s[2] != 'n' || s[3] != 'e' || s[4] != 0) return 11;
  if (n != 7) return 12;
  r = scanf("");
  if (r != 0 || feof(stdin)) return 31;
  n = 7;
  r = scanf("%n", &n);
  if (r != 0 || n != 0 || feof(stdin)) return 32;
  a = 99;
  r = scanf("%d", &a);
  if (r != EOF || a != 99 || !feof(stdin)) return 33;
  return 42;
}
`, { loadInclude });
const linkedScanf = await toolchain.instantiateLinkedWasm(linkedScanfSource, {
  exports: ["main"],
  useStdlib: true,
}, {
  stdio: { stdin: "66 2b yoQ55 2a okZ17 done" },
});
const linkedScanfResult = linkedScanf.instance.exports.main();
if (linkedScanfResult !== 42) {
  throw new Error(`linked runtime scanf family failed: ${linkedScanfResult}`);
}

const jsSnprintfSource = `
int sprintf(char *buf, const char *fmt, int n, const char *s);
int snprintf(char *buf, unsigned long size, const char *fmt, int n);
int main(void) {
  char a[16];
  char b[4];
  char c[1];
  int n = sprintf(a, "%d-%s", 12, "ok");
  int m = snprintf(b, sizeof(b), "%05d", 42);
  int z = snprintf(c, 0, "%s", "longer");
  if (n != 5) return 1;
  if (a[0] != '1' || a[1] != '2' || a[2] != '-' || a[3] != 'o' || a[4] != 'k' || a[5] != 0) return 2;
  if (m != 5) return 3;
  if (b[0] != '0' || b[1] != '0' || b[2] != '0' || b[3] != 0) return 4;
  if (z != 6) return 5;
  return 42;
}
`;
const jsSnprintf = await toolchain.instantiateLinkedWasm(jsSnprintfSource, {
  exports: ["main"],
  useStdlib: false,
});
const jsSnprintfResult = jsSnprintf.instance.exports.main();
if (jsSnprintfResult !== 42) {
  throw new Error(`JS stdio snprintf/sprintf imports did not format into wasm memory: ${jsSnprintfResult}`);
}

const linkedFloatFormatSource = await inlineStandardIncludes(`#include <stdio.h>
static int zeros(char *s, int first, int last) {
  int i;
  for (i = first; i <= last; i++) {
    if (s[i] != '0') return 0;
  }
  return 1;
}
int main(void) {
  char a[8];
  char b[8];
  char c[8];
  char d[8];
  char e[8];
  char f[16];
  char g[16];
  char h[16];
  char i[16];
  char j[16];
  char k[16];
  char ha[16];
  char hb[16];
  char hc[16];
  char hd[16];
  char he[16];
  char hf[16];
  char hg[16];
  char hh[16];
  char hi[16];
  char hj[16];
  char hk[48];
  char hl[32];
  char hm[16];
  char ho[16];
  char hp[16];
  char hq[16];
  char hr[16];
  char hs[16];
  char ht[16];
  char hu[24];
  char hv[24];
  char hw[24];
  signed char hhn = 0;
  short hn = 0;
  int in = 0;
  long ln = 0;
  long long lln = 0;
  long long jn = 0;
  long tn = 0;
  double zero = 0.0;
  double negzero = -zero;
  double inf = 1.0 / zero;
  double nanv = zero / zero;
  int n = snprintf(a, sizeof(a), "%6.1f", 3.14);
  int m = snprintf(b, sizeof(b), "%06.1f", -2.34);
  int p = snprintf(c, sizeof(c), "%6f", inf);
  int q = snprintf(d, sizeof(d), "%F", -inf);
  int r = snprintf(e, sizeof(e), "%f", nanv);
  int s = snprintf(f, sizeof(f), "%.2e", 1234.0);
  int t = snprintf(g, sizeof(g), "%10.1E", -0.0123);
  int u = snprintf(h, sizeof(h), "%010.1e", 9.99);
  int v = snprintf(i, sizeof(i), "%.4g", 1234.0);
  int w = snprintf(j, sizeof(j), "%.3g", 0.0001234);
  int x = snprintf(k, sizeof(k), "%8.2G", 12345.0);
  int ny = snprintf(ha, sizeof(ha), "%.1a", 3.0);
  int nz = snprintf(hb, sizeof(hb), "%.1A", -0.5);
  int naa = snprintf(hc, sizeof(hc), "%08.0a", 1.0);
  int nab = snprintf(hd, sizeof(hd), "%#.0f", 3.0);
  int nac = snprintf(he, sizeof(he), "%#.0e", 12.0);
  int nad = snprintf(hf, sizeof(hf), "%#.3g", 123.0);
  int nae = snprintf(hg, sizeof(hg), "%#.0a", 1.0);
  int naf = snprintf(hh, sizeof(hh), "%+.1f", 3.14);
  int nag = snprintf(hi, sizeof(hi), "% .1f", 3.14);
  int nah = snprintf(hj, sizeof(hj), "%+08.1f", 3.14);
  int nai = snprintf(hk, sizeof(hk), "%hhd:%hhu:%jd:%td:%zu",
                     130, 250, (long long)-2147483649LL, (long)-9, (unsigned long)99);
  int naj = snprintf(hl, sizeof(hl), "ab%hhncd%hnEF%nG%lnH%llnI%jnJ%tn",
                     &hhn, &hn, &in, &ln, &lln, &jn, &tn);
  int nak = snprintf(hm, sizeof(hm), "%.1f", negzero);
  int nal = snprintf(ho, sizeof(ho), "%.1e", negzero);
  int nam = snprintf(hp, sizeof(hp), "%.1g", negzero);
  int nan = snprintf(hq, sizeof(hq), "%.0a", negzero);
  int nao = snprintf(hr, sizeof(hr), "%.1g", 9.9);
  int nap = snprintf(hs, sizeof(hs), "%.2g", 99.9);
  int naq = snprintf(ht, sizeof(ht), "%.1g", 0.00009999);
  int nar = snprintf(hu, sizeof(hu), "%.12f", 1.0);
  int nas = snprintf(hv, sizeof(hv), "%.12e", 1.0);
  int nat = snprintf(hw, sizeof(hw), "%#.12g", 1.0);
  if (n != 6 || a[0] != ' ' || a[1] != ' ' || a[2] != ' ' ||
      a[3] != '3' || a[4] != '.' || a[5] != '1' || a[6] != 0) return 1;
  if (m != 6 || b[0] != '-' || b[1] != '0' || b[2] != '0' ||
      b[3] != '2' || b[4] != '.' || b[5] != '3' || b[6] != 0) return 2;
  if (p != 6 || c[0] != ' ' || c[1] != ' ' || c[2] != ' ' ||
      c[3] != 'i' || c[4] != 'n' || c[5] != 'f' || c[6] != 0) return 3;
  if (q != 4 || d[0] != '-' || d[1] != 'I' || d[2] != 'N' || d[3] != 'F' || d[4] != 0) return 4;
  if (r != 3 || e[0] != 'n' || e[1] != 'a' || e[2] != 'n' || e[3] != 0) return 5;
  if (s != 8 || f[0] != '1' || f[1] != '.' || f[2] != '2' || f[3] != '3' ||
      f[4] != 'e' || f[5] != '+' || f[6] != '0' || f[7] != '3' || f[8] != 0) return 6;
  if (t != 10 || g[0] != ' ' || g[1] != ' ' || g[2] != '-' || g[3] != '1' ||
      g[4] != '.' || g[5] != '2' || g[6] != 'E' || g[7] != '-' ||
      g[8] != '0' || g[9] != '2' || g[10] != 0) return 7;
  if (u != 10 || h[0] != '0' || h[1] != '0' || h[2] != '0' || h[3] != '1' ||
      h[4] != '.' || h[5] != '0' || h[6] != 'e' || h[7] != '+' ||
      h[8] != '0' || h[9] != '1' || h[10] != 0) return 8;
  if (v != 4 || i[0] != '1' || i[1] != '2' || i[2] != '3' || i[3] != '4' || i[4] != 0) return 9;
  if (w != 8 || j[0] != '0' || j[1] != '.' || j[2] != '0' || j[3] != '0' ||
      j[4] != '0' || j[5] != '1' || j[6] != '2' || j[7] != '3' || j[8] != 0) return 10;
  if (x != 8 || k[0] != ' ' || k[1] != '1' || k[2] != '.' || k[3] != '2' ||
      k[4] != 'E' || k[5] != '+' || k[6] != '0' || k[7] != '4' || k[8] != 0) return 11;
  if (ny != 8 || ha[0] != '0' || ha[1] != 'x' || ha[2] != '1' || ha[3] != '.' ||
      ha[4] != '8' || ha[5] != 'p' || ha[6] != '+' || ha[7] != '1' || ha[8] != 0) return 12;
  if (nz != 9 || hb[0] != '-' || hb[1] != '0' || hb[2] != 'X' || hb[3] != '1' ||
      hb[4] != '.' || hb[5] != '0' || hb[6] != 'P' || hb[7] != '-' ||
      hb[8] != '1' || hb[9] != 0) return 13;
  if (naa != 8 || hc[0] != '0' || hc[1] != '0' || hc[2] != '0' || hc[3] != 'x' ||
      hc[4] != '1' || hc[5] != 'p' || hc[6] != '+' || hc[7] != '0' ||
      hc[8] != 0) return 14;
  if (nab != 2 || hd[0] != '3' || hd[1] != '.' || hd[2] != 0) return 15;
  if (nac != 6 || he[0] != '1' || he[1] != '.' || he[2] != 'e' || he[3] != '+' ||
      he[4] != '0' || he[5] != '1' || he[6] != 0) return 16;
  if (nad != 4 || hf[0] != '1' || hf[1] != '2' || hf[2] != '3' || hf[3] != '.' ||
      hf[4] != 0) return 17;
  if (nae != 7 || hg[0] != '0' || hg[1] != 'x' || hg[2] != '1' || hg[3] != '.' ||
      hg[4] != 'p' || hg[5] != '+' || hg[6] != '0' || hg[7] != 0) return 18;
  if (naf != 4 || hh[0] != '+' || hh[1] != '3' || hh[2] != '.' || hh[3] != '1' ||
      hh[4] != 0) return 19;
  if (nag != 4 || hi[0] != ' ' || hi[1] != '3' || hi[2] != '.' || hi[3] != '1' ||
      hi[4] != 0) return 20;
  if (nah != 8 || hj[0] != '+' || hj[1] != '0' || hj[2] != '0' || hj[3] != '0' ||
      hj[4] != '0' || hj[5] != '3' || hj[6] != '.' || hj[7] != '1' ||
      hj[8] != 0) return 21;
  if (nai != 26 || hk[0] != '-' || hk[1] != '1' || hk[2] != '2' || hk[3] != '6' ||
      hk[4] != ':' || hk[5] != '2' || hk[6] != '5' || hk[7] != '0' || hk[8] != ':' ||
      hk[9] != '-' || hk[10] != '2' || hk[19] != '9' || hk[20] != ':' ||
      hk[21] != '-' || hk[22] != '9' || hk[23] != ':' || hk[24] != '9' ||
      hk[25] != '9' || hk[26] != 0) return 22;
  if (naj != 10 || hl[0] != 'a' || hl[1] != 'b' || hl[2] != 'c' ||
      hl[3] != 'd' || hl[4] != 'E' || hl[5] != 'F' || hl[6] != 'G' ||
      hl[7] != 'H' || hl[8] != 'I' || hl[9] != 'J' || hl[10] != 0) return 23;
  if (hhn != 2 || hn != 4 || in != 6 || ln != 7 || lln != 8 ||
      jn != 9 || tn != 10) return 24;
  if (nak != 4 || hm[0] != '-' || hm[1] != '0' || hm[2] != '.' ||
      hm[3] != '0' || hm[4] != 0) return 25;
  if (nal != 8 || ho[0] != '-' || ho[1] != '0' || ho[2] != '.' ||
      ho[3] != '0' || ho[4] != 'e' || ho[5] != '+' || ho[6] != '0' ||
      ho[7] != '0' || ho[8] != 0) return 26;
  if (nam != 2 || hp[0] != '-' || hp[1] != '0' || hp[2] != 0) return 27;
  if (nan != 7 || hq[0] != '-' || hq[1] != '0' || hq[2] != 'x' ||
      hq[3] != '0' || hq[4] != 'p' || hq[5] != '+' || hq[6] != '0' ||
      hq[7] != 0) return 28;
  if (nao != 5 || hr[0] != '1' || hr[1] != 'e' || hr[2] != '+' ||
      hr[3] != '0' || hr[4] != '1' || hr[5] != 0) return 29;
  if (nap != 5 || hs[0] != '1' || hs[1] != 'e' || hs[2] != '+' ||
      hs[3] != '0' || hs[4] != '2' || hs[5] != 0) return 30;
  if (naq != 6 || ht[0] != '0' || ht[1] != '.' || ht[2] != '0' ||
      ht[3] != '0' || ht[4] != '0' || ht[5] != '1' || ht[6] != 0) return 31;
  if (nar != 14 || hu[0] != '1' || hu[1] != '.' || !zeros(hu, 2, 13) ||
      hu[14] != 0) return 32;
  if (nas != 18 || hv[0] != '1' || hv[1] != '.' || !zeros(hv, 2, 13) ||
      hv[14] != 'e' || hv[15] != '+' || hv[16] != '0' || hv[17] != '0' ||
      hv[18] != 0) return 33;
  if (nat != 13 || hw[0] != '1' || hw[1] != '.' || !zeros(hw, 2, 12) ||
      hw[13] != 0) return 34;
  return 42;
}
`, { loadInclude });
const linkedFloatFormat = await toolchain.instantiateLinkedWasm(linkedFloatFormatSource, {
  exports: ["main"],
  useStdlib: true,
});
const linkedFloatFormatResult = linkedFloatFormat.instance.exports.main();
if (linkedFloatFormatResult !== 42) {
  throw new Error(`linked runtime float format padding failed: ${linkedFloatFormatResult}`);
}

const linkedMathClassSource = await inlineStandardIncludes(`#include <math.h>
int main(void) {
  double z = 0.0;
  double nanv = z / z;
  double infv = 1.0 / z;
  double nzero = -z;
  double subnormal = 1.0e-310;
  int expv = 0;
  int fexpv = 0;
  int lexpv = 0;
  double ip = 0.0;
  float fip = 0.0f;
  long double lip = 0.0L;
  if (fpclassify(nanv) != FP_NAN) return 1;
  if (fpclassify(infv) != FP_INFINITE) return 2;
  if (fpclassify(z) != FP_ZERO) return 3;
  if (fpclassify(subnormal) != FP_SUBNORMAL) return 4;
  if (fpclassify(1.0) != FP_NORMAL) return 5;
  if (!isnan(nanv) || isnan(1.0)) return 6;
  if (!isinf(infv) || isinf(nanv)) return 7;
  if (!isfinite(1.0) || isfinite(infv) || isfinite(nanv)) return 8;
  if (!isnormal(1.0) || isnormal(subnormal)) return 9;
  if (!signbit(-1.0) || !signbit(nzero) || signbit(0.0)) return 10;
  if (!isgreater(2.0, 1.0) || isgreater(nanv, 1.0)) return 11;
  if (!isgreaterequal(2.0, 2.0) || !isless(1.0, 2.0)) return 12;
  if (!islessequal(2.0, 2.0) || !islessgreater(1.0, 2.0)) return 13;
  if (islessgreater(2.0, 2.0) || !isunordered(nanv, 1.0) || isunordered(1.0, 2.0)) return 14;
  if ((int)(frexp(-8.0, &expv) * 1000.0) != -500 || expv != 4) return 15;
  if ((int)(frexpf(4.0f, &fexpv) * 1000.0f) != 500 || fexpv != 3) return 16;
  if ((int)(frexpl(16.0L, &lexpv) * 1000.0L) != 500 || lexpv != 5) return 17;
  if ((int)(ldexp(0.75, 3) * 1000.0) != 6000) return 18;
  if ((int)(ldexpf(0.5f, 4) * 1000.0f) != 8000) return 19;
  if ((int)(ldexpl(0.25L, 5) * 1000.0L) != 8000) return 20;
  if ((int)(modf(-3.75, &ip) * 100.0) != -75 || (int)ip != -3) return 21;
  if ((int)(modff(2.25f, &fip) * 100.0f) != 25 || (int)fip != 2) return 22;
  if ((int)(modfl(5.5L, &lip) * 100.0L) != 50 || (int)lip != 5) return 23;
  if ((int)copysign(2.0, nzero) != -2 || !signbit(copysign(2.0, nzero))) return 24;
  if ((int)copysignf(2.0f, -0.0f) != -2 || !signbit(copysignf(2.0f, -0.0f))) return 25;
  if ((int)copysignl(2.0L, -0.0L) != -2 || !signbit(copysignl(2.0L, -0.0L))) return 26;
  if (!isnan(nan("")) || !isnan(nanf("")) || !isnan(nanl(""))) return 27;
  return 42;
}
`, { loadInclude });
const linkedMathClass = await toolchain.instantiateLinkedWasm(linkedMathClassSource, {
  exports: ["main"],
  useStdlib: true,
});
const linkedMathClassResult = linkedMathClass.instance.exports.main();
if (linkedMathClassResult !== 42) {
  throw new Error(`linked runtime math classification failed: ${linkedMathClassResult}`);
}

const jsBasicStdioSource = `
int fputs(const char *s, void *stream);
int fputc(int c, void *stream);
int fflush(void *stream);
unsigned long fwrite(const void *ptr, unsigned long size, unsigned long nmemb, void *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, void *stream);
long write(int fd, const void *buf, unsigned long count);
long lseek(int fd, long offset, int whence);
int main(void) {
  char buf[4];
  if (fputs("A", (void *)1) != 1) return 1;
  if (fputc('B', (void *)1) != 'B') return 2;
  if (fputs("E", (void *)2) != 1) return 3;
  if (fputc('R', (void *)2) != 'R') return 4;
  if (fflush((void *)1) != 0) return 5;
  if (fwrite("CD", 1, 2, (void *)1) != 2) return 6;
  if (fwrite("!", 1, 1, (void *)2) != 1) return 7;
  if (fputs("q", (void *)0) != 1) return 8;
  if (fputc('r', (void *)0) != 'r') return 9;
  if (fwrite("s", 1, 1, (void *)0) != 1) return 10;
  if (write(1, "W", 1) != 1) return 11;
  if (write(2, "e", 1) != 1) return 12;
  if (write(0, "n", 1) != -1) return 13;
  if (lseek(1, 0, 0) != -1) return 14;
  if (fread(buf, 1, sizeof(buf), (void *)0) != 0) return 15;
  return 42;
}
`;
let jsBasicStdout = "";
let jsBasicStderr = "";
const jsBasicStdio = await toolchain.instantiateLinkedWasm(jsBasicStdioSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsBasicStdout += chunk; },
  onStderr: (chunk) => { jsBasicStderr += chunk; },
});
const jsBasicStdioResult = jsBasicStdio.instance.exports.main();
if (jsBasicStdioResult !== 42 || jsBasicStdout !== "ABCDW" || jsBasicStderr !== "ER!qrse") {
  throw new Error(
    `JS basic stdio imports failed: result=${jsBasicStdioResult}, stdout=${JSON.stringify(jsBasicStdout)}, stderr=${JSON.stringify(jsBasicStderr)}`,
  );
}

const jsStdinSource = `
int fgetc(void *stream);
int getchar(void);
char *fgets(char *s, int size, void *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, void *stream);
int feof(void *stream);
int ferror(void *stream);
void clearerr(void *stream);
void perror(const char *s);
int main(void) {
  char line[4];
  char rest[4];
  if (fgetc((void *)0) != 'a') return 1;
  if (getchar() != '\\n') return 2;
  if (fgets(line, sizeof(line), (void *)0) != line) return 3;
  if (line[0] != 'b' || line[1] != 'c' || line[2] != '\\n' || line[3] != 0) return 4;
  if (fread(rest, 2, 2, (void *)0) != 2) return 5;
  if (rest[0] != 'd' || rest[1] != 'e' || rest[2] != 'f' || rest[3] != 'g') return 6;
  if (feof((void *)0) != 0) return 7;
  if (fgetc((void *)0) != -1) return 7;
  if (feof((void *)0) != 1) return 8;
  if (ferror((void *)0) != 0) return 9;
  clearerr((void *)0);
  if (feof((void *)0) != 0) return 10;
  perror("stdin");
  return 42;
}
`;
let jsStdinStderr = "";
const jsStdin = await toolchain.instantiateLinkedWasm(jsStdinSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  stdio: { stdin: "a\nbc\ndefg" },
  onStderr: (chunk) => { jsStdinStderr += chunk; },
});
const jsStdinResult = jsStdin.instance.exports.main();
if (jsStdinResult !== 42 || jsStdinStderr !== "stdin: error\n") {
  throw new Error(`JS stdin imports failed: result=${jsStdinResult}, stderr=${JSON.stringify(jsStdinStderr)}`);
}

const jsWideStdioSource = `
int fgetwc(void *stream);
int getwc(void *stream);
int getwchar(void);
int fputwc(int wc, void *stream);
int putwc(int wc, void *stream);
int putwchar(int wc);
int ungetwc(int wc, void *stream);
int *fgetws(int *s, int n, void *stream);
int fputws(const int *s, void *stream);
int fwide(void *stream, int mode);
int main(void) {
  int line[4];
  int text[] = {0x3042, '!', 0};
  if (fwide((void *)0, 1) != 1) return 1;
  if (fwide((void *)0, -1) != -1) return 2;
  if (fwide((void *)0, 0) != 0) return 3;
  if (fputwc('A', (void *)1) != 'A') return 4;
  if (putwc(0x3042, (void *)1) != 0x3042) return 5;
  if (fputws(text, (void *)2) != 2) return 6;
  if (putwchar('Q') != 'Q') return 7;
  if (fgetwc((void *)0) != 'x') return 8;
  if (ungetwc('Y', (void *)0) != 'Y') return 9;
  if (getwc((void *)0) != 'Y') return 10;
  if (getwchar() != 0x3042) return 11;
  if (fgetws(line, 4, (void *)0) != line) return 12;
  if (line[0] != '\\n' || line[1] != 0) return 13;
  if (ungetwc(0x3042, (void *)0) != -1) return 14;
  if (fgetwc((void *)0) != -1) return 15;
  return 42;
}
`;
let jsWideStdout = "";
let jsWideStderr = "";
const jsWideStdio = await toolchain.instantiateLinkedWasm(jsWideStdioSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  stdio: { stdin: "xあ\n" },
  onStdout: (chunk) => { jsWideStdout += chunk; },
  onStderr: (chunk) => { jsWideStderr += chunk; },
});
const jsWideStdioResult = jsWideStdio.instance.exports.main();
if (jsWideStdioResult !== 42 || jsWideStdout !== "AあQ" || jsWideStderr !== "あ!") {
  throw new Error(
    `JS wide stdio imports failed: result=${jsWideStdioResult}, stdout=${JSON.stringify(jsWideStdout)}, stderr=${JSON.stringify(jsWideStderr)}`,
  );
}

try {
  toolchain.compileLinkedWasm([
    "int main(void) { return other(); }\n",
    "int other(void){return 42２2;}\n",
  ], {
    exports: ["main"],
    useStdlib: false,
  });
  throw new Error("invalid UTF-8 token source unexpectedly compiled");
} catch (err) {
  const message = err && err.message ? err.message : String(err);
  if (!message.includes("source 2:") || !message.includes("E2028")) {
    throw new Error(`invalid token diagnostic was not surfaced: ${JSON.stringify(message)}`);
  }
  if (message.includes("%*s")) {
    throw new Error(`invalid token diagnostic leaked printf format text: ${JSON.stringify(message)}`);
  }
}

const stdioSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) { return 42; }
`, { loadInclude });
const stdioWat = toolchain.compileWat(stdioSource);
if (!stdioWat.includes("(return (i32.const 42))")) {
  throw new Error("standard include expansion did not compile stdio.h in the JS pipeline");
}

const complexSource = await inlineStandardIncludes(
  await readFile("test/fixtures/stdheader/complex_ops.c", "utf8"),
  { loadInclude },
);
const complexObj = toolchain.compileObject(complexSource);
const complexObjPath = path.join(outDir, "complex_ops_from_compiler_api.o");
await writeFile(complexObjPath, complexObj);
try {
  const dump = execFileSync("wasm-objdump", ["-x", complexObjPath], { encoding: "utf8" });
  if (!dump.includes("F <__ag_complex_sqrt>")) {
    throw new Error("compiler API object is missing __ag_complex_sqrt definition");
  }
  if (dump.includes("@Q0") || dump.includes("env.__ag_complex_sqrt")) {
    throw new Error("compiler API object has a corrupted or unresolved __ag_complex_sqrt symbol");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

console.log(`ag_c wasm JS compile+link pipeline smoke: ok (${linkedPath})`);
