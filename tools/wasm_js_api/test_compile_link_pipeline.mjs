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
int main(void) {
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
int main(void) {
  printf("%*s:%03d:aa", 4, "x", 7);
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
if (linkedStdioStdout !== "   x:007:aa") {
  throw new Error(`instantiated stdio import pipeline stdout mismatch: ${JSON.stringify(linkedStdioStdout)}`);
}
if (linkedStdioStderr === "") linkedStdioStderr = linkedStdio.readStderr();
if (linkedStdioStderr !== "runtime: error\n") {
  throw new Error(`instantiated stdio import pipeline stderr mismatch: ${JSON.stringify(linkedStdioStderr)}`);
}

const linkedStdioErrorSource = await inlineStandardIncludes(`#include <stdio.h>
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
  long n1 = getline(&line, &cap, stdin);
  if (n1 != 6 || line[0] != 'f' || line[4] != 't' || line[5] != '\\n' || line[6] != 0) return 1;
  long n2 = getline(&line, &cap, stdin);
  if (n2 != 7 || line[0] != 's' || line[6] != '\\n' || line[7] != 0) return 2;
  long n3 = getline(&line, &cap, stdin);
  if (n3 != -1 || !feof(stdin)) return 3;
  if (cap < 8) return 4;
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
  r = scanf("%d %x %2s%c%n", &a, &x, s, &c, &n);
  if (r != 4) return 5;
  if (a != 55 || x != 42) return 6;
  if (s[0] != 'o' || s[1] != 'k' || s[2] != 0) return 7;
  if (c != 'Z' || n != 9) return 8;

  a = 0;
  s[0] = s[1] = s[2] = s[3] = s[4] = 0;
  n = 0;
  r = fscanf(stdin, "%d %4s%n", &a, s, &n);
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
  stdio: { stdin: "55 2a okZ17 done" },
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
