import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { createToolchain } from "./agc-toolchain.js";
import { inlineStandardIncludes } from "./agc-include-inline.js";

const compilerWasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const linkerWasmPath = process.argv[3] || "build/wasm_linker_selfhost/ag_wasm_link.wasm";
const outDir = "build/wasm_js_pipeline_smoke";
await mkdir(outDir, { recursive: true });

const compilerWasm = await readFile(compilerWasmPath);
const linkerWasm = await readFile(linkerWasmPath);
const runtimeObject = await readFile("build/libagc_runtime.o");
async function freshToolchain() {
  return createToolchain({
    compilerWasm,
    linkerWasm,
    runtimeObject,
  });
}
let toolchain = await freshToolchain();
const loadInclude = async (name) => readFile(new URL(`../../include/${name}`, import.meta.url), "utf8");

const mainSource = "int other(void); int main(void) { return other() + 1; }\n";
const otherSource = "int other(void) { return 41; }\n";
const mainObj = toolchain.compileObject(mainSource);
const otherObj = toolchain.compileObject(otherSource);

const selfHostGlobalSources = [
  "int value; void start(void) {} void update(void) {}\n",
  "int value = 7; void start(void) {} void update(void) {}\n",
  "static int value; void start(void) {} void update(void) {}\n",
  "void start(void) { static int value; value++; } void update(void) {}\n",
];
for (const source of selfHostGlobalSources) {
  const object = toolchain.compileObject(source);
  if (object[0] !== 0x00 || object[1] !== 0x61 || object[2] !== 0x73 || object[3] !== 0x6d) {
    throw new Error(`self-host global object is not a wasm object: ${source}`);
  }
}

const globalStarterSource = `
int value = 1;
void start(void) { value = 4; }
void update(void) { value++; }
int read_value(void) { return value; }
`;
const globalStarter = await toolchain.instantiateLinkedWasm(globalStarterSource, {
  exports: ["start", "update", "read_value"],
  useStdlib: false,
});
globalStarter.instance.exports.start();
globalStarter.instance.exports.update();
if (globalStarter.instance.exports.read_value() !== 5) {
  throw new Error("self-host global starter game did not preserve global state");
}

const signedStarter = await toolchain.instantiateLinkedWasm(
  "void start(void) {} void update(void) {}\n",
  {
    exports: [
      { name: "start", signature: "v()" },
      { name: "update", signature: "v()" },
    ],
    useStdlib: false,
  },
);
signedStarter.instance.exports.start();
signedStarter.instance.exports.update();

async function expectSignedExportFailure(source, exportSpec, expectedMessage) {
  const isolatedToolchain = await freshToolchain();
  try {
    isolatedToolchain.compileLinkedWasm(source, {
      exports: [exportSpec],
      useStdlib: false,
    });
    throw new Error(`signed export unexpectedly linked: ${exportSpec.name}`);
  } catch (err) {
    if (err.message.startsWith("signed export unexpectedly linked:") ||
        !err.message.includes(expectedMessage)) {
      throw err;
    }
  }
}

await expectSignedExportFailure(
  "int start(void) { return 0; }\n",
  { name: "start", signature: "v()" },
  "export C signature mismatch for start: expected v(), actual i32()",
);
await expectSignedExportFailure(
  "void start(int value) { (void)value; }\n",
  { name: "start", signature: "v()" },
  "export C signature mismatch for start: expected v(), actual v(i32)",
);
await expectSignedExportFailure(
  "void start(double value) { (void)value; }\n",
  { name: "start", signature: "v(i32)" },
  "export C signature mismatch for start: expected v(i32), actual v(f64)",
);
await expectSignedExportFailure(
  "void start(unsigned int value) { (void)value; }\n",
  { name: "start", signature: "v(i32)" },
  "export C signature mismatch for start: expected v(i32), actual v(u32)",
);
await expectSignedExportFailure(
  "int start;\n",
  { name: "start", signature: "v()" },
  "signed export refers to a data symbol: start",
);
await expectSignedExportFailure(
  "void host_entry(void); void call_host(void) { host_entry(); }\n",
  { name: "host_entry", signature: "v()" },
  "signed export refers to an import-only function: host_entry",
);
await expectSignedExportFailure(
  "extern int external_data; int read_external(void) { return external_data; }\n",
  { name: "external_data", signature: "v()" },
  "signed export refers to an undefined data symbol: external_data",
);
await expectSignedExportFailure(
  "void present(void) {}\n",
  { name: "missing", signature: "v()" },
  "signed export not found: missing",
);

const canonicalTypedefExports = await toolchain.instantiateLinkedWasm(`
typedef void entry_t(void);
typedef int (*callback_t)(int);
entry_t typed_start;
void typed_start(void) {}
void accept_callback(callback_t callback) { (void)callback; }
`, {
  exports: [
    { name: "typed_start", signature: "v()" },
    { name: "accept_callback", signature: "v(p<i32(i32)>)" },
  ],
  useStdlib: false,
});
canonicalTypedefExports.instance.exports.typed_start();

const declarationDefinitionExports = await toolchain.instantiateLinkedWasm([
  "typedef void entry_t(void); entry_t split_start; void invoke_start(void) { split_start(); }\n",
  "void split_start(void) {}\n",
], {
  exports: [
    { name: "split_start", signature: "v()" },
    { name: "invoke_start", signature: "v()" },
  ],
  useStdlib: false,
});
declarationDefinitionExports.instance.exports.invoke_start();

const legacyStringExport = await toolchain.instantiateLinkedWasm(
  "int legacy(int value) { return value + 1; }\n",
  { exports: ["legacy"], useStdlib: false },
);
if (legacyStringExport.instance.exports.legacy(41) !== 42) {
  throw new Error("legacy string export behavior changed");
}

const virtualHeaderProgram = await toolchain.instantiateLinkedWasm({
  name: "main.c",
  source: '#include "player.h"\nint main(void) { return PLAYER_VALUE; }\n',
}, {
  headers: { "player.h": "#define PLAYER_VALUE 42\n" },
  exports: ["main"],
  useStdlib: false,
});
if (virtualHeaderProgram.instance.exports.main() !== 42) {
  throw new Error("virtual project header did not survive compile/link/instantiate");
}

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

const namedLinked = toolchain.compileLinkedWasm([
  { name: "main.c", source: mainSource },
  { name: "player.c", source: otherSource },
], {
  exports: ["main"],
  useStdlib: false,
});
if (namedLinked[0] !== 0x00 || namedLinked[1] !== 0x61 ||
    namedLinked[2] !== 0x73 || namedLinked[3] !== 0x6d) {
  throw new Error("named source pipeline output is not a wasm module");
}

const warningLinked = toolchain.compileLinkedWasmWithDiagnostics([
  { name: "first.c", source: "int first(void) { int x = 1.5; return x; }\n" },
  { name: "second.c", source: "int second(void) { int y = 2.5; return y; }\n" },
], {
  exports: ["first", "second"],
  useStdlib: false,
});
if (warningLinked.wasm[0] !== 0x00 || warningLinked.wasm[1] !== 0x61 ||
    warningLinked.wasm[2] !== 0x73 || warningLinked.wasm[3] !== 0x6d) {
  throw new Error("diagnostic pipeline output is not a wasm module");
}
if (warningLinked.sourceDiagnostics.length !== 2 ||
    warningLinked.sourceDiagnostics[0].sourceName !== "first.c" ||
    warningLinked.sourceDiagnostics[1].sourceName !== "second.c" ||
    warningLinked.sourceDiagnostics[0].diagnostics[0]?.code !== "W3010" ||
    warningLinked.sourceDiagnostics[1].diagnostics[0]?.code !== "W3010") {
  throw new Error(`source warnings were not retained separately: ${JSON.stringify(warningLinked.sourceDiagnostics)}`);
}
if (warningLinked.diagnostics.length !== 2 ||
    warningLinked.diagnostics[0].sourceId !== 0 || warningLinked.diagnostics[1].sourceId !== 1) {
  throw new Error(`flattened diagnostic order is unstable: ${JSON.stringify(warningLinked.diagnostics)}`);
}
if (!Object.isFrozen(warningLinked.diagnostics) ||
    !Object.isFrozen(warningLinked.sourceDiagnostics) ||
    !Object.isFrozen(warningLinked.sourceDiagnostics[0].diagnostics)) {
  throw new Error("linked diagnostics are not immutable snapshots");
}
const linkedWarningSnapshot = JSON.stringify(warningLinked);
toolchain.compileObject("int after_warnings(void) { return 0; }\n");
if (JSON.stringify(warningLinked) !== linkedWarningSnapshot) {
  throw new Error("a later toolchain compile changed linked diagnostic snapshots");
}

try {
  toolchain.compileLinkedWasm([
    { name: "same.c", source: mainSource },
    { name: "same.c", source: otherSource },
  ], { useStdlib: false });
  throw new Error("duplicate source names unexpectedly compiled");
} catch (err) {
  if (!(err instanceof TypeError) || !err.message.includes("duplicate source name: same.c")) {
    throw err;
  }
}

try {
  toolchain.compileLinkedWasm([
    { name: "main.c", source: mainSource },
    { name: "player.c", source: "int other(void) { const int x = 1; x = 2; return x; }\n" },
  ], { useStdlib: false });
  throw new Error("named semantic error unexpectedly compiled");
} catch (err) {
  const diagnostic = err.diagnostics?.[0];
  if (err.sourceIndex !== 1 || diagnostic?.sourceId !== 1 ||
      diagnostic?.sourceName !== "player.c" || diagnostic?.code !== "E3077") {
    throw new Error(`named source identity was not preserved: ${JSON.stringify(err.diagnostics)}`);
  }
}

try {
  toolchain.compileLinkedWasmWithDiagnostics([
    { name: "warning-before-error.c", source: "int warned(void) { int x = 1.5; return x; }\n" },
    { name: "linked-error.c", source: "int failed(void) { const int x = 1; x = 2; return x; }\n" },
  ], { useStdlib: false });
  throw new Error("diagnostic link error unexpectedly compiled");
} catch (err) {
  if (err.message === "diagnostic link error unexpectedly compiled") throw err;
  if (err.sourceIndex !== 1 || err.diagnostics?.[0]?.code !== "E3077" ||
      err.sourceDiagnostics?.length !== 2 ||
      err.sourceDiagnostics[0].diagnostics[0]?.code !== "W3010" ||
      err.sourceDiagnostics[1].diagnostics[0]?.code !== "E3077" ||
      !Object.isFrozen(err.sourceDiagnostics)) {
    throw new Error(`diagnostics before a later source error were lost: ${JSON.stringify(err.sourceDiagnostics)}`);
  }
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
float sinf(float);
long double sinl(long double);
double cos(double);
float cosf(float);
long double cosl(long double);
double tan(double);
float tanf(float);
long double tanl(long double);
double sqrt(double);
float sqrtf(float);
long double sqrtl(long double);
double pow(double, double);
float powf(float, float);
long double powl(long double, long double);
double exp(double);
float expf(float);
long double expl(long double);
double log(double);
float logf(float);
long double logl(long double);
double log2(double);
double log10(double);
double atan2(double, double);
float atan2f(float, float);
long double atan2l(long double, long double);
double fmod(double, double);
float fmodf(float, float);
long double fmodl(long double, long double);
double hypot(double, double);
float hypotf(float, float);
long double hypotl(long double, long double);
double exp2(double);
float exp2f(float);
long double exp2l(long double);
double expm1(double);
float expm1f(float);
long double expm1l(long double);
double cbrt(double);
float cbrtf(float);
long double cbrtl(long double);
double erf(double);
float erff(float);
long double erfl(long double);
double erfc(double);
float erfcf(float);
long double erfcl(long double);
double log1p(double);
float log1pf(float);
long double log1pl(long double);
double sinh(double);
float sinhf(float);
long double sinhl(long double);
double cosh(double);
float coshf(float);
long double coshl(long double);
double tanh(double);
float tanhf(float);
long double tanhl(long double);
double asinh(double);
float asinhf(float);
long double asinhl(long double);
double acosh(double);
float acoshf(float);
long double acoshl(long double);
double atanh(double);
float atanhf(float);
long double atanhl(long double);
double atan(double);
float atanf(float);
long double atanl(long double);
double asin(double);
float asinf(float);
long double asinl(long double);
double acos(double);
float acosf(float);
long double acosl(long double);
double floor(double);
float floorf(float);
long double floorl(long double);
double ceil(double);
float ceilf(float);
long double ceill(long double);
double round(double);
float roundf(float);
long double roundl(long double);
double trunc(double);
float truncf(float);
long double truncl(long double);
double nearbyint(double);
float nearbyintf(float);
long double nearbyintl(long double);
double rint(double);
float rintf(float);
long double rintl(long double);
long lrint(double);
long lrintf(float);
long lrintl(long double);
long long llrint(double);
long long llrintf(float);
long long llrintl(long double);
long lround(double);
long lroundf(float);
long lroundl(long double);
long long llround(double);
long long llroundf(float);
long long llroundl(long double);
double remainder(double, double);
float remainderf(float, float);
long double remainderl(long double, long double);
double remquo(double, double, int *);
float remquof(float, float, int *);
long double remquol(long double, long double, int *);
double fdim(double, double);
float fdimf(float, float);
long double fdiml(long double, long double);
double fmin(double, double);
float fminf(float, float);
long double fminl(long double, long double);
double fmax(double, double);
float fmaxf(float, float);
long double fmaxl(long double, long double);
double fma(double, double, double);
float fmaf(float, float, float);
long double fmal(long double, long double, long double);
double frexp(double, int *);
float frexpf(float, int *);
long double frexpl(long double, int *);
double ldexp(double, int);
float ldexpf(float, int);
long double ldexpl(long double, int);
double scalbn(double, int);
float scalbnf(float, int);
long double scalbnl(long double, int);
double scalbln(double, long);
float scalblnf(float, long);
long double scalblnl(long double, long);
int ilogb(double);
int ilogbf(float);
int ilogbl(long double);
double logb(double);
float logbf(float);
long double logbl(long double);
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
  int dquo = 0;
  int fquo = 0;
  int lquo = 0;
  int dquo_bits = 0;
  int dquo_neg_bits = 0;
  double ip = 0.0;
  float fip = 0.0f;
  long double lip = 0.0L;
  if (fpclassify(nanv) != 0 || fpclassify(infv) != 1 || fpclassify(z) != 2) return 1;
  if (fpclassify(subnormal) != 3 || fpclassify(1.0) != 4) return 2;
  if (!isnan(nanv) || isnan(1.0) || !isinf(infv) || isinf(nanv)) return 3;
  if (!isfinite(1.0) || isfinite(infv) || isfinite(nanv)) return 4;
  if (!isnormal(1.0) || isnormal(subnormal)) return 5;
  if (!signbit(-1.0) || !signbit(nzero) || signbit(0.0)) return 6;
  if ((int)(sqrt(2.0) * 1000.0) != 1414 || (int)(sqrtf(2.0f) * 1000.0f) != 1414) return 94;
  if ((int)(sqrtl(2.0L) * 1000.0L) != 1414) return 95;
  if (!isnan(sqrt(-1.0)) || !isnan(sqrtf(-1.0f)) || !isnan(sqrtl(-1.0L))) return 96;
  if (!isnan(sqrt(nanv)) || !signbit(sqrt(nzero)) || sqrt(infv) <= 1.0e300) return 97;
  if (sqrt(1.0e200) <= 9.9e99 || sqrt(1.0e200) >= 1.01e100) return 149;
  if (sqrt(1.0e-200) <= 9.9e-101 || sqrt(1.0e-200) >= 1.01e-100) return 150;
  if (sqrtf(1.0e20f) <= 9.9e9f || sqrtf(1.0e20f) >= 1.01e10f) return 151;
  if (!isnan(pow(-2.0, 0.5)) || !isnan(powf(-2.0f, 0.5f)) || !isnan(powl(-2.0L, 0.5L))) return 98;
  if (!isnan(pow(nanv, 2.0)) || (int)pow(nanv, 0.0) != 1) return 99;
  if (pow(z, -0.5) <= 1.0e300 || !signbit(pow(nzero, 3.0))) return 100;
  if (pow(nzero, -3.0) >= -1.0e300) return 101;
  if (pow(-2.0, 10000000000.0) <= 1.0e300 || pow(-2.0, 10000000001.0) >= -1.0e300) return 127;
  if (pow(2.0, -10000000000.0) != 0.0 || !signbit(pow(-2.0, -10000000001.0))) return 128;
  if (!signbit(pow(nzero, 10000000001.0)) || pow(nzero, -10000000001.0) >= -1.0e300) return 129;
  if (log(z) >= -1.0e300 || log(infv) <= 1.0e300) return 102;
  if (!isnan(log(-1.0)) || !isnan(logf(-1.0f)) || !isnan(logl(-1.0L))) return 103;
  if (!isnan(log(nanv)) || log2(z) >= -1.0e300 || !isnan(log2(-1.0))) return 104;
  if (log10(infv) <= 1.0e300 || !isnan(log10(-1.0))) return 105;
  if (log1p(-1.0) >= -1.0e300 || !isnan(log1p(-2.0))) return 106;
  if (log1p(1.0e-20) <= 0.0 || log1p(1.0e-20) >= 2.0e-20) return 155;
  if (log1p(-1.0e-20) >= 0.0 || log1p(-1.0e-20) <= -2.0e-20) return 156;
  if (exp(infv) <= 1.0e300 || exp(-infv) != 0.0) return 107;
  if (!isnan(exp(nanv)) || !isnan(expf((float)nanv)) || !isnan(expl((long double)nanv))) return 108;
  if (exp2(infv) <= 1.0e300 || exp2(-infv) != 0.0) return 109;
  if (expm1(infv) <= 1.0e300 || (int)expm1(-infv) != -1) return 110;
  if (!signbit(expm1(nzero)) || !signbit(expm1f(-0.0f)) || !signbit(expm1l(-0.0L))) return 139;
  if (exp(10000.0) <= 1.0e300 || exp(-10000.0) != 0.0) return 140;
  if (exp2(2000.0) <= 1.0e300 || exp2(-2000.0) != 0.0) return 141;
  if ((int)expm1(-10000.0) != -1) return 142;
  if (expm1(1.0e-20) <= 0.0 || expm1(1.0e-20) >= 2.0e-20) return 157;
  if (expm1(-1.0e-20) >= 0.0 || expm1(-1.0e-20) <= -2.0e-20) return 158;
  if (!isnan(sin(infv)) || !isnan(sinf((float)infv)) || !isnan(sinl((long double)infv))) return 111;
  if (!isnan(cos(infv)) || !isnan(cosf((float)infv)) || !isnan(cosl((long double)infv))) return 112;
  if (!isnan(tan(infv)) || !isnan(tanf((float)infv)) || !isnan(tanl((long double)infv))) return 113;
  if (sin(10000.0) != sin(10000.0) || sin(10000.0) <= -2.0 || sin(10000.0) >= 2.0) return 146;
  if (cos(10000.0) != cos(10000.0) || cos(10000.0) <= -2.0 || cos(10000.0) >= 2.0) return 147;
  if (tan(10000.0) != tan(10000.0) || tan(10000.0) <= -100.0 || tan(10000.0) >= 100.0) return 148;
  if (sinh(infv) <= 1.0e300 || sinh(-infv) >= -1.0e300) return 114;
  if (cosh(infv) <= 1.0e300 || cosh(-infv) <= 1.0e300) return 115;
  if ((int)tanh(infv) != 1 || (int)tanh(-infv) != -1) return 116;
  if (!signbit(sinh(nzero)) || !signbit(tanh(nzero)) || !signbit(asinh(nzero))) return 169;
  if (sinh(1.0e-20) <= 0.0 || sinh(1.0e-20) >= 2.0e-20) return 170;
  if (sinh(-1.0e-20) >= 0.0 || sinh(-1.0e-20) <= -2.0e-20) return 171;
  if (tanh(1.0e-20) <= 0.0 || tanh(1.0e-20) >= 2.0e-20) return 172;
  if (tanh(-1.0e-20) >= 0.0 || tanh(-1.0e-20) <= -2.0e-20) return 173;
  if (sinh(10000.0) <= 1.0e300 || sinh(-10000.0) >= -1.0e300) return 143;
  if (cosh(10000.0) <= 1.0e300 || cosh(-10000.0) <= 1.0e300) return 144;
  if ((int)tanh(10000.0) != 1 || (int)tanh(-10000.0) != -1) return 145;
  if (acosh(infv) <= 1.0e300 || !isnan(acosh(0.5)) || !isnan(acoshf(0.5f))) return 117;
  if (atanh(1.0) <= 1.0e300 || atanh(-1.0) >= -1.0e300) return 118;
  if (!isnan(atanh(2.0)) || !isnan(atanhl(2.0L))) return 119;
  if (!signbit(atanh(nzero))) return 159;
  if (atanh(1.0e-20) <= 0.0 || atanh(1.0e-20) >= 2.0e-20) return 160;
  if (atanh(-1.0e-20) >= 0.0 || atanh(-1.0e-20) <= -2.0e-20) return 161;
  if ((int)(atan(1.0) * 1000.0) < 783 || (int)(atan(1.0) * 1000.0) > 787) return 162;
  if ((int)(atan(-1.0) * 1000.0) > -783 || (int)(atan(-1.0) * 1000.0) < -787) return 163;
  if (!signbit(atan(nzero))) return 164;
  if (atan(infv) <= 1.56 || atan(infv) >= 1.58 || atan(-infv) >= -1.56 || atan(-infv) <= -1.58) return 165;
  if (asinh(1.0e200) <= 400.0 || asinh(1.0e200) >= 500.0) return 123;
  if (asinh(-1.0e200) >= -400.0 || asinh(-1.0e200) <= -500.0) return 124;
  if (asinh(1.0e-20) <= 0.0 || asinh(1.0e-20) >= 2.0e-20) return 174;
  if (asinh(-1.0e-20) >= 0.0 || asinh(-1.0e-20) <= -2.0e-20) return 175;
  if (acosh(1.0e200) <= 400.0 || acosh(1.0e200) >= 500.0) return 125;
  if (acoshl(1.0e200L) <= 400.0L || acoshl(1.0e200L) >= 500.0L) return 126;
  if (cbrt(infv) <= 1.0e300 || cbrt(-infv) >= -1.0e300) return 120;
  if (!isnan(cbrt(nanv)) || !isnan(cbrtf((float)nanv))) return 121;
  if (cbrtl((long double)infv) <= 1.0e300L || !signbit(cbrt(nzero))) return 122;
  if (cbrt(1.0e300) <= 9.9e99 || cbrt(1.0e300) >= 1.01e100) return 152;
  if (cbrt(-1.0e300) >= -9.9e99 || cbrt(-1.0e300) <= -1.01e100) return 153;
  if (cbrt(1.0e-300) <= 9.9e-101 || cbrt(1.0e-300) >= 1.01e-100) return 154;
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
  if ((int)(scalbn(0.75, 4) * 1000.0) != 12000) return 17;
  if ((int)(scalbnf(0.5f, 5) * 1000.0f) != 16000) return 18;
  if ((int)(scalbnl(0.25L, 6) * 1000.0L) != 16000) return 19;
  if ((int)(scalbln(1.5, 3L) * 1000.0) != 12000) return 20;
  if ((int)(scalblnf(1.25f, 2L) * 1000.0f) != 5000) return 21;
  if ((int)(scalblnl(3.0L, -1L) * 1000.0L) != 1500) return 22;
  if (ldexp(1.0, 5000) <= 1.0e300 || !signbit(ldexp(-1.0, -5000))) return 127;
  if (scalbln(1.0, 5000L) <= 1.0e300 || !signbit(scalbln(-1.0, -5000L))) return 128;
  if (scalblnf(1.0f, 5000L) <= 1.0e30f) return 129;
  if (!signbit(scalblnl(-1.0L, -5000L))) return 130;
  if (ilogb(8.0) != 3 || ilogbf(0.75f) != -1 || ilogbl(0.25L) != -2) return 23;
  if ((int)logb(8.0) != 3 || (int)logbf(0.75f) != -1 || (int)logbl(0.25L) != -2) return 24;
  if ((int)(fmod(7.5, 2.0) * 1000.0) != 1500 || (int)(fmodf(7.5f, 2.0f) * 1000.0f) != 1500) return 25;
  if ((int)(fmodl(7.5L, 2.0L) * 1000.0L) != 1500) return 26;
  if (!isnan(fmod(7.5, z)) || !isnan(fmod(infv, 2.0))) return 27;
  if ((int)fmod(7.5, infv) != 7 || !signbit(fmod(-z, 3.0))) return 28;
  if (fmod(1.0e20, 3.0) < 0.0 || fmod(1.0e20, 3.0) >= 3.0) return 133;
  if (fmod(-1.0e20, 3.0) > 0.0 || fmod(-1.0e20, 3.0) <= -3.0) return 134;
  if (fmodl(1.0e20L, 3.0L) < 0.0L || fmodl(1.0e20L, 3.0L) >= 3.0L) return 135;
  if ((int)(hypot(3.0, 4.0) * 1000.0) != 5000 || (int)(hypotf(3.0f, 4.0f) * 1000.0f) != 5000) return 29;
  if ((int)(hypotl(3.0L, 4.0L) * 1000.0L) != 5000) return 30;
  if (hypot(1.0e200, 1.0e200) <= 1.0e200) return 31;
  if (hypot(infv, nanv) <= 1.0e300 || !isnan(hypot(nanv, 3.0))) return 32;
  if (!signbit(atan2(-z, z))) return 33;
  if ((int)(atan2(z, -z) * 1000.0) < 3140 || (int)(atan2(z, -z) * 1000.0) > 3143) return 34;
  if ((int)(atan2(infv, infv) * 1000.0) < 783 || (int)(atan2(infv, infv) * 1000.0) > 787) return 35;
  if ((int)(atan2(infv, -infv) * 1000.0) < 2354 || (int)(atan2(infv, -infv) * 1000.0) > 2358) return 36;
  if ((int)(asin(1.0) * 1000.0) < 1568 || (int)(asin(1.0) * 1000.0) > 1572) return 37;
  if ((int)(asin(-1.0) * 1000.0) > -1568 || (int)(asin(-1.0) * 1000.0) < -1572) return 182;
  if (!signbit(asin(nzero))) return 183;
  if (!isnan(asin(2.0)) || !isnan(asinf(2.0f)) || !isnan(asinl(2.0L))) return 38;
  if ((int)(acos(0.0) * 1000.0) < 1568 || (int)(acos(0.0) * 1000.0) > 1572) return 39;
  if (acos(1.0) != 0.0 || signbit(acos(1.0))) return 184;
  if ((int)(acos(-1.0) * 1000.0) < 3140 || (int)(acos(-1.0) * 1000.0) > 3143) return 185;
  if (!isnan(acos(2.0)) || !isnan(acosf(2.0f)) || !isnan(acosl(2.0L))) return 40;
  if ((int)(modf(-3.75, &ip) * 100.0) != -75 || (int)ip != -3) return 17;
  if ((int)(modff(2.25f, &fip) * 100.0f) != 25 || (int)fip != 2) return 18;
  if ((int)(modfl(5.5L, &lip) * 100.0L) != 50 || (int)lip != 5) return 19;
  if (!signbit(modf(nzero, &ip)) || !signbit(ip)) return 141;
  if (!signbit(modff(-2.0f, &fip)) || (int)fip != -2) return 142;
  if (!signbit(modfl(-0.0L, &lip)) || !signbit(lip)) return 143;
  if ((int)copysign(2.0, nzero) != -2 || !signbit(copysign(2.0, nzero))) return 20;
  if ((int)copysignf(2.0f, -0.0f) != -2 || !signbit(copysignf(2.0f, -0.0f))) return 21;
  if ((int)copysignl(2.0L, -0.0L) != -2 || !signbit(copysignl(2.0L, -0.0L))) return 22;
  if (signbit(fabs(nzero)) || signbit(fabsf(-0.0f)) || signbit(fabsl(-0.0L))) return 131;
  if (signbit(copysign(nzero, 1.0)) || signbit(copysignf(-0.0f, 1.0f)) ||
      signbit(copysignl(-0.0L, 1.0L))) return 132;
  if (!isnan(nan("")) || !isnan(nanf("")) || !isnan(nanl(""))) return 23;
  if ((int)(fdim(5.5, 2.0) * 1000.0) != 3500 || (int)(fdim(2.0, 5.5) * 1000.0) != 0) return 24;
  if ((int)(fdimf(5.5f, 2.0f) * 1000.0f) != 3500) return 25;
  if ((int)(fdiml(5.5L, 2.0L) * 1000.0L) != 3500) return 26;
  if ((int)(fma(2.0, 3.0, 0.5) * 1000.0) != 6500) return 27;
  if ((int)(fmaf(2.0f, 3.0f, 0.5f) * 1000.0f) != 6500) return 28;
  if ((int)(fmal(2.0L, 3.0L, 0.5L) * 1000.0L) != 6500) return 29;
  if ((int)(remainder(5.5, 2.0) * 1000.0) != -500) return 30;
  if ((int)(remainderf(5.5f, 2.0f) * 1000.0f) != -500) return 31;
  if ((int)(remainderl(5.5L, 2.0L) * 1000.0L) != -500) return 32;
  if ((int)(remquo(5.5, 2.0, &dquo) * 1000.0) != -500 || dquo != 3) return 33;
  if ((int)(remquof(5.5f, 2.0f, &fquo) * 1000.0f) != -500 || fquo != 3) return 34;
  if ((int)(remquol(5.5L, 2.0L, &lquo) * 1000.0L) != -500 || lquo != 3) return 35;
  if ((int)(remquo(19.5, 2.0, &dquo_bits) * 1000.0) != -500 || dquo_bits != 2) return 36;
  if ((int)(remquo(-19.5, 2.0, &dquo_neg_bits) * 1000.0) != 500 || dquo_neg_bits != -2) return 37;
  if (remainder(1.0e20, 3.0) < -1.5 || remainder(1.0e20, 3.0) > 1.5) return 136;
  if (remquo(1.0e20, 3.0, &dquo_bits) < -1.5 || remquo(1.0e20, 3.0, &dquo_bits) > 1.5 ||
      dquo_bits < -7 || dquo_bits > 7) return 137;
  if (remquo(-1.0e20, 3.0, &dquo_neg_bits) < -1.5 || remquo(-1.0e20, 3.0, &dquo_neg_bits) > 1.5 ||
      dquo_neg_bits < -7 || dquo_neg_bits > 7) return 138;
  if ((int)(exp2(3.0) * 1000.0) < 7998 || (int)(exp2(3.0) * 1000.0) > 8002) return 38;
  if ((int)(exp2f(3.0f) * 1000.0f) < 7998 || (int)(exp2f(3.0f) * 1000.0f) > 8002) return 39;
  if ((int)(exp2l(3.0L) * 1000.0L) < 7998 || (int)(exp2l(3.0L) * 1000.0L) > 8002) return 40;
  if ((int)(expm1(1.0) * 1000.0) < 1716 || (int)(expm1(1.0) * 1000.0) > 1720) return 41;
  if ((int)(expm1f(1.0f) * 1000.0f) < 1716 || (int)(expm1f(1.0f) * 1000.0f) > 1720) return 42;
  if ((int)(expm1l(1.0L) * 1000.0L) < 1716 || (int)(expm1l(1.0L) * 1000.0L) > 1720) return 43;
  if ((int)(log1p(1.0) * 1000.0) < 691 || (int)(log1p(1.0) * 1000.0) > 695) return 44;
  if ((int)(log1pf(1.0f) * 1000.0f) < 691 || (int)(log1pf(1.0f) * 1000.0f) > 695) return 45;
  if ((int)(log1pl(1.0L) * 1000.0L) < 691 || (int)(log1pl(1.0L) * 1000.0L) > 695) return 46;
  if (!signbit(log1p(nzero)) || !signbit(log1pf(-0.0f)) || !signbit(log1pl(-0.0L))) return 140;
  if ((int)(sinh(1.0) * 1000.0) < 1174 || (int)(sinh(1.0) * 1000.0) > 1176) return 47;
  if ((int)(sinhf(1.0f) * 1000.0f) < 1174 || (int)(sinhf(1.0f) * 1000.0f) > 1176) return 48;
  if ((int)(sinhl(1.0L) * 1000.0L) < 1174 || (int)(sinhl(1.0L) * 1000.0L) > 1176) return 49;
  if ((int)(cosh(1.0) * 1000.0) < 1542 || (int)(cosh(1.0) * 1000.0) > 1544) return 50;
  if ((int)(coshf(1.0f) * 1000.0f) < 1542 || (int)(coshf(1.0f) * 1000.0f) > 1544) return 51;
  if ((int)(coshl(1.0L) * 1000.0L) < 1542 || (int)(coshl(1.0L) * 1000.0L) > 1544) return 52;
  if ((int)(tanh(1.0) * 1000.0) < 760 || (int)(tanh(1.0) * 1000.0) > 762) return 53;
  if ((int)(tanhf(1.0f) * 1000.0f) < 760 || (int)(tanhf(1.0f) * 1000.0f) > 762) return 54;
  if ((int)(tanhl(1.0L) * 1000.0L) < 760 || (int)(tanhl(1.0L) * 1000.0L) > 762) return 55;
  if ((int)(asinh(1.0) * 1000.0) < 880 || (int)(asinh(1.0) * 1000.0) > 882) return 56;
  if ((int)(asinhf(1.0f) * 1000.0f) < 880 || (int)(asinhf(1.0f) * 1000.0f) > 882) return 57;
  if ((int)(asinhl(1.0L) * 1000.0L) < 880 || (int)(asinhl(1.0L) * 1000.0L) > 882) return 58;
  if ((int)(acosh(2.0) * 1000.0) < 1315 || (int)(acosh(2.0) * 1000.0) > 1317) return 59;
  if ((int)(acoshf(2.0f) * 1000.0f) < 1315 || (int)(acoshf(2.0f) * 1000.0f) > 1317) return 60;
  if ((int)(acoshl(2.0L) * 1000.0L) < 1315 || (int)(acoshl(2.0L) * 1000.0L) > 1317) return 61;
  if ((int)(atanh(0.5) * 1000.0) < 548 || (int)(atanh(0.5) * 1000.0) > 550) return 62;
  if ((int)(atanhf(0.5f) * 1000.0f) < 548 || (int)(atanhf(0.5f) * 1000.0f) > 550) return 63;
  if ((int)(atanhl(0.5L) * 1000.0L) < 548 || (int)(atanhl(0.5L) * 1000.0L) > 550) return 64;
  if ((int)(erf(1.0) * 1000.0) < 841 || (int)(erf(1.0) * 1000.0) > 844) return 65;
  if ((int)(erff(1.0f) * 1000.0f) < 841 || (int)(erff(1.0f) * 1000.0f) > 844) return 66;
  if ((int)(erfl(1.0L) * 1000.0L) < 841 || (int)(erfl(1.0L) * 1000.0L) > 844) return 67;
  if (erf(z) != 0.0 || !signbit(erf(nzero))) return 176;
  if (erff(0.0f) != 0.0f || !signbit(erff(-0.0f))) return 177;
  if (erfl(0.0L) != 0.0L || !signbit(erfl(-0.0L))) return 178;
  if ((int)(erfc(1.0) * 1000.0) < 156 || (int)(erfc(1.0) * 1000.0) > 158) return 68;
  if ((int)(erfcf(1.0f) * 1000.0f) < 156 || (int)(erfcf(1.0f) * 1000.0f) > 158) return 69;
  if ((int)(erfcl(1.0L) * 1000.0L) < 156 || (int)(erfcl(1.0L) * 1000.0L) > 158) return 70;
  if (erfc(z) != 1.0 || erfc(nzero) != 1.0) return 179;
  if (erfcf(0.0f) != 1.0f || erfcf(-0.0f) != 1.0f) return 180;
  if (erfcl(0.0L) != 1.0L || erfcl(-0.0L) != 1.0L) return 181;
  if (erfc(10.0) <= 0.0 || erfc(10.0) >= 1.0e-40) return 166;
  if (erfcf(5.0f) <= 0.0f || erfcf(5.0f) >= 1.0e-10f) return 167;
  if (erfcl(10.0L) <= 0.0L || erfcl(10.0L) >= 1.0e-40L) return 168;
  if (nearbyint(2.5) != 2.0 || nearbyintf(-2.5f) != -2.0f || nearbyintl(3.5L) != 4.0L) return 71;
  if (rint(3.5) != 4.0 || rintf(2.5f) != 2.0f || rintl(-3.5L) != -4.0L) return 72;
  if (lrint(3.5) != 4 || lrintf(2.5f) != 2 || lrintl(-3.5L) != -4) return 73;
  if (llrint(2.5) != 2 || llrintf(-2.5f) != -2 || llrintl(3.5L) != 4) return 74;
  if (lround(2.5) != 3 || lroundf(-2.5f) != -3 || lroundl(3.5L) != 4) return 75;
  if (llround(-3.5) != -4 || llroundf(2.5f) != 3 || llroundl(-2.5L) != -3) return 76;
  if (!isnan(floor(nanv)) || ceil(infv) <= 1.0e300 || round(-infv) >= -1.0e300) return 92;
  if (!signbit(floor(-z)) || !signbit(trunc(-0.8)) || !signbit(ceil(-0.8)) || !signbit(round(-0.3))) return 93;
  if (!isnan(floorf((float)nanv)) || ceill((long double)infv) <= 1.0e300L) return 94;
  if (!signbit(roundl(-0.3L))) return 95;
  if (trunc(10000000000.75) != 10000000000.0 || floor(-10000000000.75) != -10000000001.0) return 118;
  if (ceil(10000000000.25) != 10000000001.0 || round(-10000000000.25) != -10000000000.0) return 119;
  if (trunc(1.0e20) != 1.0e20 || floor(1.0e20) != 1.0e20) return 120;
  if (ceil(-1.0e20) != -1.0e20 || round(-1.0e20) != -1.0e20) return 121;
  if (nearbyint(10000000000.5) != 10000000000.0 || rint(10000000001.5) != 10000000002.0) return 126;
  if ((int)fmin(nanv, 7.0) != 7 || (int)fmin(7.0, nanv) != 7) return 77;
  if ((int)fminf((float)nanv, 5.0f) != 5 || (int)fminl(6.0L, (long double)nanv) != 6) return 78;
  if ((int)fmax(nanv, 7.0) != 7 || (int)fmax(7.0, nanv) != 7) return 79;
  if ((int)fmaxf((float)nanv, 5.0f) != 5 || (int)fmaxl(6.0L, (long double)nanv) != 6) return 80;
  if (!signbit(fmin(-z, z)) || !signbit(fmin(z, -z))) return 81;
  if (signbit(fmax(-z, z)) || signbit(fmax(z, -z))) return 82;
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
      !dump.includes("env.pow") ||
      !dump.includes("env.sinhf") ||
      !dump.includes("env.coshl") ||
      !dump.includes("env.tanhl") ||
      !dump.includes("env.asinh") ||
      !dump.includes("env.acoshl") ||
      !dump.includes("env.atan") ||
      !dump.includes("env.atanhf") ||
      !dump.includes("env.scalbn") ||
      !dump.includes("env.scalblnl") ||
      !dump.includes("env.ilogb") ||
      !dump.includes("env.logbl") ||
      !dump.includes("env.erf") ||
      !dump.includes("env.erfcl") ||
      !dump.includes("env.lrint") ||
      !dump.includes("env.llroundl")) {
    throw new Error("linked math wasm did not import JS math helpers");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}
const mathInstantiated = await toolchain.instantiateLinkedWasm(mathSource, {
  exports: ["main"],
  useStdlib: false,
});
const mathImportResult = mathInstantiated.instance.exports.main();
if (mathImportResult !== 1010) {
  throw new Error(`instantiated math pipeline did not use JS math imports: ${mathImportResult}`);
}

const linkedStdioSource = await inlineStandardIncludes(`#include <stdio.h>
#include <errno.h>
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
  errno = 0;
  perror("runtime");
  errno = 5;
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
if (linkedStdioStderr !== "runtime: no error\nruntime: error\n") {
  throw new Error(`instantiated stdio import pipeline stderr mismatch: ${JSON.stringify(linkedStdioStderr)}`);
}

const linkedStdioErrorSource = await inlineStandardIncludes(`#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
struct stat {
  unsigned short st_mode;
  long st_size;
};
int fstat(int fd, struct stat *st);
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
static int write_one(char *path, int ch) {
  FILE *f = fopen(path, "w");
  if (!f) return 0;
  if (fputc(ch, f) != ch) return 0;
  return fclose(f) == 0;
}
static int write_many(char *path, int count, int ch) {
  FILE *f = fopen(path, "w");
  int i = 0;
  if (!f) return 0;
  while (i < count) {
    if (fputc(ch, f) != ch) return 0;
    i++;
  }
  return fclose(f) == 0;
}
int main(void) {
  char b[8];
  char long_path[] = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmn.txt";
  int large_i = 0;
  fpos_t removed_pos = 0;
  if (!write_one("keep.txt", 'K')) return 181;
  if (!write_one("hot.txt", 'H')) return 182;
  FILE *reuse_keep = fopen("keep.txt", "r");
  int reuse_fd = open("keep.txt", O_RDONLY);
  if (!reuse_keep || reuse_fd < 0) return 183;
  if (!write_one("one.txt", '1')) return 184;
  if (!write_one("two.txt", '2')) return 185;
  if (!write_one("three.txt", '3')) return 186;
  errno = 0;
  if (fgetc(reuse_keep) != 'K' || fgetc(reuse_keep) != EOF || ferror(reuse_keep)) return 187;
  errno = 0;
  if (read(reuse_fd, b, 1) != 1 || b[0] != 'K') return 188;
  if (fclose(reuse_keep) != 0) return 189;
  if (close(reuse_fd) != 0) return 190;
  FILE *reuse_r0 = fopen("reuse_r0.txt", "w");
  FILE *reuse_r1 = fopen("reuse_r1.txt", "w");
  FILE *reuse_r2 = fopen("reuse_r2.txt", "w");
  FILE *reuse_r3 = fopen("reuse_r3.txt", "w");
  if (!reuse_r0 || !reuse_r1 || !reuse_r2 || !reuse_r3) return 193;
  errno = 0;
  if (fopen("reuse_r4.txt", "w") != NULL || errno != ENOMEM) return 194;
  if (fclose(reuse_r0) != 0 || fclose(reuse_r1) != 0 ||
      fclose(reuse_r2) != 0 || fclose(reuse_r3) != 0) return 195;
  if (!write_one("guard.txt", 'G')) return 208;
  FILE *g0 = fopen("guard.txt", "r");
  FILE *g1 = fopen("guard.txt", "r");
  FILE *g2 = fopen("guard.txt", "r");
  FILE *g3 = fopen("guard.txt", "r");
  FILE *g4 = fopen("guard.txt", "r");
  FILE *g5 = fopen("guard.txt", "r");
  FILE *g6 = fopen("guard.txt", "r");
  FILE *g7 = fopen("guard.txt", "r");
  if (!g0 || !g1 || !g2 || !g3 || !g4 || !g5 || !g6 || !g7) return 209;
  errno = 0;
  if (fopen("guard.txt", "w") != NULL || errno != ENOMEM) return 210;
  errno = 0;
  if (fopen(long_path, "w") != NULL || errno != ENAMETOOLONG) return 281;
  errno = 0;
  if (tmpfile() != NULL || errno != ENOMEM) return 218;
  if (fgetc(g0) != 'G') return 211;
  if (fclose(g0) != 0 || fclose(g1) != 0 || fclose(g2) != 0 ||
      fclose(g3) != 0 || fclose(g4) != 0 || fclose(g5) != 0 ||
      fclose(g6) != 0 || fclose(g7) != 0) return 212;
  if (!write_one("fdguard.txt", 'F')) return 213;
  int fg0 = open("fdguard.txt", O_RDONLY);
  int fg1 = open("fdguard.txt", O_RDONLY);
  int fg2 = open("fdguard.txt", O_RDONLY);
  int fg3 = open("fdguard.txt", O_RDONLY);
  int fg4 = open("fdguard.txt", O_RDONLY);
  int fg5 = open("fdguard.txt", O_RDONLY);
  int fg6 = open("fdguard.txt", O_RDONLY);
  int fg7 = open("fdguard.txt", O_RDONLY);
  if (fg0 < 0 || fg1 < 0 || fg2 < 0 || fg3 < 0 ||
      fg4 < 0 || fg5 < 0 || fg6 < 0 || fg7 < 0) return 214;
  errno = 0;
  if (open("fdguard.txt", O_RDWR | O_TRUNC) != -1 || errno != ENOMEM) return 215;
  errno = 0;
  if (open(long_path, O_RDWR | O_CREAT) != -1 || errno != ENAMETOOLONG) return 282;
  if (read(fg0, b, 1) != 1 || b[0] != 'F') return 216;
  if (close(fg0) != 0 || close(fg1) != 0 || close(fg2) != 0 ||
      close(fg3) != 0 || close(fg4) != 0 || close(fg5) != 0 ||
      close(fg6) != 0 || close(fg7) != 0) return 217;
  if (!write_one("freopen0.txt", 'A')) return 219;
  int fd_freopen = open("freopen0.txt", O_RDONLY);
  if (fd_freopen < 0) return 220;
  FILE *fr0 = fdopen(fd_freopen, "r");
  if (!fr0) return 221;
  if (!write_one("freopen1.txt", 'B')) return 222;
  FILE *fr1 = fopen("freopen1.txt", "r");
  if (!fr1) return 223;
  if (!write_one("freopen2.txt", 'C')) return 224;
  FILE *fr2 = fopen("freopen2.txt", "r");
  if (!fr2) return 225;
  if (!write_one("freopen3.txt", 'D')) return 226;
  FILE *fr3 = fopen("freopen3.txt", "r");
  if (!fr3) return 227;
  errno = 0;
  if (freopen("freopen4.txt", "w", fr0) != NULL || errno != ENOMEM) return 228;
  if (read(fd_freopen, b, 1) != 1 || b[0] != 'A') return 229;
  if (fclose(fr0) != 0 || fclose(fr1) != 0 ||
      fclose(fr2) != 0 || fclose(fr3) != 0) return 230;
  if (!write_one("rename_old.txt", 'O')) return 234;
  FILE *rename_old_full = fopen("rename_old.txt", "r");
  if (!rename_old_full) return 235;
  if (!write_one("rename_dst.txt", 'D')) return 236;
  FILE *rename_dst_full = fopen("rename_dst.txt", "r");
  if (!rename_dst_full) return 237;
  if (!write_one("rename_extra1.txt", '1')) return 238;
  FILE *rename_extra1 = fopen("rename_extra1.txt", "r");
  if (!rename_extra1) return 239;
  if (!write_one("rename_extra2.txt", '2')) return 240;
  FILE *rename_extra2 = fopen("rename_extra2.txt", "r");
  if (!rename_extra2) return 241;
  errno = 0;
  if (rename("rename_old.txt", "rename_dst.txt") != -1 || errno != ENOMEM) return 242;
  if (fgetc(rename_old_full) != 'O' || fgetc(rename_dst_full) != 'D') return 243;
  if (fclose(rename_old_full) != 0 || fclose(rename_dst_full) != 0 ||
      fclose(rename_extra1) != 0 || fclose(rename_extra2) != 0) return 244;
  if (!write_many("rename_large_dst.txt", 300, 'D')) return 255;
  if (!write_one("rename_small_old.txt", 's')) return 256;
  FILE *rename_large_dst = fopen("rename_large_dst.txt", "r");
  if (!rename_large_dst) return 257;
  if (rename("rename_small_old.txt", "rename_large_dst.txt") != 0) return 258;
  large_i = 0;
  while (large_i < 300) {
    if (fgetc(rename_large_dst) != 'D') return 259;
    large_i++;
  }
  if (fgetc(rename_large_dst) != EOF || ferror(rename_large_dst)) return 260;
  if (freopen("rename_after_freopen.txt", "w", rename_large_dst) != rename_large_dst) return 267;
  large_i = 0;
  while (large_i < 300) {
    if (fputc('R', rename_large_dst) != 'R') return 268;
    large_i++;
  }
  if (fclose(rename_large_dst) != 0) return 261;
  FILE *rename_small_new = fopen("rename_large_dst.txt", "r");
  if (!rename_small_new) return 262;
  if (fgetc(rename_small_new) != 's' || fgetc(rename_small_new) != EOF) return 263;
  if (fclose(rename_small_new) != 0) return 264;
  FILE *rename_after_freopen = fopen("rename_after_freopen.txt", "r");
  if (!rename_after_freopen) return 269;
  large_i = 0;
  while (large_i < 300) {
    if (fgetc(rename_after_freopen) != 'R') return 270;
    large_i++;
  }
  if (fgetc(rename_after_freopen) != EOF || ferror(rename_after_freopen)) return 271;
  if (fclose(rename_after_freopen) != 0) return 272;
  if (remove("rename_after_freopen.txt") != 0) return 273;
  if (remove("rename_large_dst.txt") != 0) return 265;
  if (!write_many("big.txt", 300, 'B')) return 197;
  FILE *other = fopen("other.txt", "w");
  if (!other) return 198;
  large_i = 0;
  while (large_i < 256) {
    if (fputc('x', other) != 'x') return 199;
    large_i++;
  }
  errno = 0;
  if (fputc('x', other) != EOF || errno != ENOMEM || !ferror(other)) return 200;
  if (fclose(other) != 0) return 201;
  FILE *big = fopen("big.txt", "r");
  if (!big) return 202;
  large_i = 0;
  while (large_i < 300) {
    if (fgetc(big) != 'B') return 203;
    large_i++;
  }
  if (fgetc(big) != EOF || ferror(big)) return 204;
  if (fclose(big) != 0) return 205;
  if (remove("big.txt") != 0) return 206;
  if (remove("other.txt") != 0) return 207;
  if (!write_many("rename_big_old.txt", 300, 'L')) return 245;
  if (!write_one("rename_big_dst.txt", 'd')) return 246;
  FILE *rename_big_dst = fopen("rename_big_dst.txt", "r");
  if (!rename_big_dst) return 247;
  if (rename("rename_big_old.txt", "rename_big_dst.txt") != 0) return 248;
  if (fgetc(rename_big_dst) != 'd' || fgetc(rename_big_dst) != EOF) return 249;
  if (fclose(rename_big_dst) != 0) return 250;
  FILE *rename_big_new = fopen("rename_big_dst.txt", "r");
  if (!rename_big_new) return 251;
  large_i = 0;
  while (large_i < 300) {
    if (fgetc(rename_big_new) != 'L') return 252;
    large_i++;
  }
  if (fgetc(rename_big_new) != EOF || ferror(rename_big_new)) return 253;
  if (fclose(rename_big_new) != 0) return 254;
  errno = 0;
  if (fopen(NULL, "r") != NULL || errno != EINVAL) return 80;
  FILE *wf = fopen("tmp.txt", "w");
  if (!wf) return 1;
  if (fwrite("A", 1, 1, wf) != 1) return 2;
  if (fseek(wf, 0, SEEK_SET) != 0) return 3;
  errno = 0;
  if (fread(b, 1, 1, wf) != 0) return 4;
  if (errno != EBADF) return 81;
  if (!ferror(wf)) return 5;
  clearerr(wf);
  if (ferror(wf)) return 6;
  if (fclose(wf) != 0) return 7;

  FILE *rf = fopen("tmp.txt", "r");
  if (!rf) return 8;
  errno = 0;
  if (fwrite("B", 1, 1, rf) != 0) return 9;
  if (errno != EBADF) return 82;
  if (fputs("B", rf) != EOF) return 10;
  if (fputc('B', rf) != EOF) return 11;
  if (!ferror(rf)) return 12;
  clearerr(rf);
  if (ferror(rf)) return 13;
  if (fclose(rf) != 0) return 14;
  errno = 0;
  if (fprintf(NULL, "B") != EOF || errno != EBADF) return 94;
  errno = 0;
  if (fputs("B", NULL) != EOF || errno != EBADF) return 95;
  errno = 0;
  if (fputc('B', NULL) != EOF || errno != EBADF) return 96;
  errno = 0;
  if (fwrite("B", 1, 1, NULL) != 0 || errno != EBADF) return 97;
  errno = 0;
  if (fgetc(stdout) != EOF || errno != EBADF) return 98;
  errno = 0;
  if (fread(b, 1, 1, stdout) != 0 || errno != EBADF) return 99;
  errno = 0;
  if (fgetc((FILE *)3) != EOF || errno != EBADF) return 100;
  errno = 0;
  if (fprintf((FILE *)3, "B%d", 1) != EOF || errno != EBADF) return 113;
  errno = 0;
  if (fwrite("B", 1, 1, (FILE *)3) != 0 || errno != EBADF) return 101;
  errno = 0;
  if (fflush((FILE *)3) != EOF || errno != EBADF) return 102;
  errno = 0;
  if (fclose((FILE *)3) != EOF || errno != EBADF) return 103;
  errno = 0;
  if (ftell((FILE *)3) != -1 || errno != EBADF) return 104;
  errno = 0;
  if (feof((FILE *)3) != 0 || errno != EBADF) return 105;
  errno = 0;
  if (ferror((FILE *)3) != 1 || errno != EBADF) return 106;
  errno = 0;
  clearerr((FILE *)3);
  if (errno != EBADF) return 107;
  int scan_value = 77;
  errno = 0;
  if (fscanf((FILE *)3, "%d", &scan_value) != EOF || errno != EBADF || scan_value != 77) return 108;
  char *line = NULL;
  size_t line_cap = 0;
  errno = 0;
  if (getline(&line, &line_cap, (FILE *)3) != -1 || errno != EBADF) return 109;
  FILE *gw = fopen("tmp.txt", "w");
  if (!gw) return 110;
  errno = 0;
  if (getline(&line, &line_cap, gw) != -1 || errno != EBADF) return 111;
  if (fclose(gw) != 0) return 112;
  errno = 0;
  if (remove(NULL) == 0 || errno != EINVAL) return 15;
  FILE *held = fopen("tmp.txt", "r");
  if (!held) return 165;
  if (remove("tmp.txt") != 0) return 16;
  errno = 0;
  if (fgetc(held) != EOF || errno != EBADF || !ferror(held)) return 166;
  errno = 0;
  if (fread(b, 1, 1, held) != 0 || errno != EBADF || !ferror(held)) return 167;
  errno = 0;
  if (fseek(held, 0, SEEK_SET) != -1 || errno != EBADF || !ferror(held)) return 168;
  errno = 0;
  if (ftell(held) != -1 || errno != EBADF || !ferror(held)) return 169;
  errno = 0;
  if (fgetpos(held, &removed_pos) != -1 || errno != EBADF || !ferror(held)) return 170;
  errno = 0;
  rewind(held);
  if (errno != EBADF || !ferror(held)) return 171;
  errno = 0;
  if (ungetc('Q', held) != EOF || errno != EBADF || !ferror(held)) return 172;
  errno = 0;
  if (fflush(held) != EOF || errno != EBADF || !ferror(held)) return 274;
  errno = 0;
  if (setvbuf(held, NULL, _IONBF, 0) != EOF || errno != EBADF || !ferror(held)) return 275;
  if (fclose(held) != 0) return 173;
  errno = 0;
  rf = fopen("tmp.txt", "r");
  if (rf != NULL || errno != ENOENT) return 17;

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
  FILE *rename_src_touch = fopen("tmp.txt", "a");
  if (!rename_src_touch) return 191;
  if (fputc('C', rename_src_touch) != 'C' || fclose(rename_src_touch) != 0) return 192;
  FILE *rename_held = fopen("tmp.txt", "r");
  int fd_rename_held = open("tmp.txt", O_RDONLY);
  if (!rename_held || fd_rename_held < 0) return 176;
  FILE *rename_dst = fopen("dst.txt", "w");
  if (!rename_dst) return 174;
  if (fputc('D', rename_dst) != 'D' || fclose(rename_dst) != 0) return 175;
  FILE *rename_dst_held = fopen("dst.txt", "r");
  int fd_rename_dst_held = open("dst.txt", O_RDONLY);
  if (!rename_dst_held || fd_rename_dst_held < 0) return 231;
  if (rename("tmp.txt", "dst.txt") != 0) return 177;
  if (fgetc(rename_held) != 'A' || fgetc(rename_held) != 'B' ||
      fgetc(rename_held) != 'C' || fgetc(rename_held) != EOF) return 178;
  if (read(fd_rename_held, b, 3) != 3 ||
      b[0] != 'A' || b[1] != 'B' || b[2] != 'C') return 179;
  if (fgetc(rename_dst_held) != 'D' || fgetc(rename_dst_held) != EOF) return 232;
  if (fclose(rename_dst_held) != 0) return 266;
  if (read(fd_rename_dst_held, b, 1) != 1 || b[0] != 'D') return 233;
  if (fclose(rename_held) != 0 || close(fd_rename_held) != 0 ||
      close(fd_rename_dst_held) != 0) return 180;
  errno = 0;
  if (rename(NULL, "new.txt") == 0 || errno != EINVAL) return 29;
  errno = 0;
  if (rename("tmp.txt", NULL) == 0 || errno != EINVAL) return 30;
  if (rename("dst.txt", "new.txt") != 0) return 31;
  uf = fopen("new.txt", "r");
  if (!uf) return 32;
  if (fgetc(uf) != 'A') return 33;
  if (fclose(uf) != 0) return 34;
  errno = 0;
  int excl_existing = open("new.txt", O_CREAT | O_EXCL);
  if (excl_existing >= 0 || errno != EEXIST) return 119;
  if (remove("new.txt") != 0) return 120;
  int excl_new = open("new.txt", O_CREAT | O_EXCL);
  if (excl_new < 0) return 121;
  if (close(excl_new) != 0) return 122;
  errno = 0;
  if (fopen(long_path, "w") != NULL || errno != ENAMETOOLONG) return 276;
  errno = 0;
  if (open(long_path, O_RDWR | O_CREAT) != -1 || errno != ENAMETOOLONG) return 277;
  errno = 0;
  if (remove(long_path) == 0 || errno != ENAMETOOLONG) return 278;
  errno = 0;
  if (rename(long_path, "short_long.txt") == 0 || errno != ENAMETOOLONG) return 279;
  errno = 0;
  if (rename("new.txt", long_path) == 0 || errno != ENAMETOOLONG) return 280;
  int fd_ro = open("new.txt", O_RDONLY);
  if (fd_ro < 0) return 123;
  errno = 0;
  if (write(fd_ro, "x", 1) != -1 || errno != EBADF) return 124;
  errno = 0;
  if (fdopen(fd_ro, "w") != NULL || errno != EBADF) return 125;
  if (close(fd_ro) != 0) return 126;
  int fd_wo = open("new.txt", O_WRONLY);
  if (fd_wo < 0) return 127;
  errno = 0;
  if (read(fd_wo, b, 1) != -1 || errno != EBADF) return 128;
  errno = 0;
  if (fdopen(fd_wo, "r") != NULL || errno != EBADF) return 129;
  if (close(fd_wo) != 0) return 130;
  int fd_rw = open("new.txt", O_RDWR);
  if (fd_rw < 0) return 131;
  if (write(fd_rw, "Z", 1) != 1) return 132;
  if (lseek(fd_rw, 0, SEEK_SET) != 0) return 133;
  if (read(fd_rw, b, 1) != 1 || b[0] != 'Z') return 134;
  errno = 0;
  if (write(fd_rw, "x", (unsigned long)-1) != -1 || errno != EINVAL) return 149;
  errno = 0;
  if (read(fd_rw, b, (unsigned long)-1) != -1 || errno != EINVAL) return 150;
  if (close(fd_rw) != 0) return 135;
  int fd_ro_trunc = open("new.txt", O_RDONLY | O_TRUNC);
  if (fd_ro_trunc < 0) return 151;
  if (read(fd_ro_trunc, b, 1) != 1 || b[0] != 'Z') return 152;
  if (close(fd_ro_trunc) != 0) return 153;
  int fd_gap = open("new.txt", O_RDWR);
  if (fd_gap < 0) return 154;
  if (lseek(fd_gap, 3, SEEK_SET) != 3) return 155;
  if (write(fd_gap, "W", 1) != 1) return 156;
  if (lseek(fd_gap, 0, SEEK_SET) != 0) return 157;
  if (read(fd_gap, b, 4) != 4 || b[0] != 'Z' || b[1] != 0 ||
      b[2] != 0 || b[3] != 'W') return 158;
  if (close(fd_gap) != 0) return 159;
  int fd_app = open("new.txt", O_RDWR | O_APPEND);
  if (fd_app < 0) return 136;
  if (lseek(fd_app, 0, SEEK_SET) != 0) return 137;
  if (write(fd_app, "Y", 1) != 1) return 138;
  if (lseek(fd_app, 0, SEEK_SET) != 0) return 139;
  if (read(fd_app, b, 5) != 5 || b[0] != 'Z' || b[1] != 0 ||
      b[2] != 0 || b[3] != 'W' || b[4] != 'Y') return 140;
  if (close(fd_app) != 0) return 141;
  FILE *af = fopen("new.txt", "a");
  if (!af) return 142;
  if (fseek(af, 0, SEEK_SET) != 0) return 143;
  if (fwrite("Q", 1, 1, af) != 1) return 144;
  if (fclose(af) != 0) return 145;
  fd_rw = open("new.txt", O_RDONLY);
  if (fd_rw < 0) return 146;
  if (read(fd_rw, b, 6) != 6 || b[0] != 'Z' || b[1] != 0 || b[2] != 0 ||
      b[3] != 'W' || b[4] != 'Y' || b[5] != 'Q') return 147;
  if (close(fd_rw) != 0) return 148;
  int fd_removed = open("new.txt", O_RDONLY);
  if (fd_removed < 0) return 160;
  if (remove("new.txt") != 0) return 161;
  struct stat removed_st;
  errno = 0;
  if (fstat(fd_removed, &removed_st) != -1 || errno != EBADF) return 162;
  errno = 0;
  if (lseek(fd_removed, 0, SEEK_SET) != -1 || errno != EBADF) return 163;
  if (close(fd_removed) != 0) return 164;
  char iobuf[BUFSIZ];
  if (setvbuf(stdout, NULL, _IONBF, 0) != 0) return 35;
  errno = 0;
  if (setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf)) == 0 || errno != ENOSYS) return 36;
  if (setvbuf(stdout, iobuf, 99, sizeof(iobuf)) == 0) return 37;

  uf = fopen("tmp.txt", "w");
  if (!uf) return 38;
  errno = 0;
  if (setvbuf(uf, iobuf, _IOFBF, sizeof(iobuf)) == 0 || errno != ENOSYS) return 39;
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
  FILE *full = fopen("tmp.txt", "w");
  if (!full) return 114;
  size_t fill_i = 0;
  while (fill_i < 65536) {
    if (fputc('x', full) != 'x') return 115;
    fill_i++;
  }
  errno = 0;
  if (fputc('y', full) != EOF || errno != ENOMEM || !ferror(full)) return 116;
  clearerr(full);
  errno = 0;
  if (fwrite("z", 1, 1, full) != 0 || errno != ENOMEM || !ferror(full)) return 117;
  if (fclose(full) != 0) return 118;
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
  time_t sunday = 259200;
  time_t monday = 345600;
  time_t before_epoch = -1;
  struct tm *epoch_tm = localtime(&epoch);
  int epoch_ok = epoch_tm && epoch_tm->tm_year == 70 && epoch_tm->tm_mon == 0 &&
                 epoch_tm->tm_mday == 1 && epoch_tm->tm_wday == 4;
  struct tm *tm = gmtime(&sample);
  struct tm *sun_tm;
  struct tm *mon_tm;
  struct tm *btm;
  char buf[64];
  wchar_t wbuf[64];
  wchar_t wfmt[] = {'%', 'F', ' ', '%', 'T', 0};
  wchar_t wfmt_iso[] = {'%', 'G', '-', '%', 'V', '-', '%', 'u', ' ', '%', 'R', ' ', '%', 'z', 0};
  size_t n;
  size_t wn;
  struct tm mk = {0};
  struct tm nmk = {0};
  struct tm bmk = {0};
  struct timespec ts = {-1, -1};
  if (!epoch_ok) return 1;
  if (!tm || tm->tm_sec != 1 || tm->tm_min != 1 || tm->tm_hour != 1 ||
      tm->tm_mday != 2 || tm->tm_wday != 5 || tm->tm_yday != 1) return 2;
  if (timespec_get(&ts, TIME_UTC) != 0 || ts.tv_sec != -1 || ts.tv_nsec != -1) return 10;
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
  wn = wcsftime(wbuf, 64, wfmt_iso, tm);
  if (wn != 21 || wbuf[0] != '1' || wbuf[3] != '0' || wbuf[4] != '-' ||
      wbuf[7] != '-' || wbuf[8] != '5' || wbuf[10] != '0' || wbuf[14] != '1' ||
      wbuf[16] != '+' || wbuf[20] != '0' || wbuf[21] != 0) return 19;
  n = strftime(buf, sizeof(buf), "%C %D %R %r %u %G %g %V %z%n%t", tm);
  if (n != 50 || strcmp(buf, "19 01/02/70 01:01 01:01:01 AM 5 1970 70 01 +0000\\n\\t") != 0) return 17;
  sun_tm = gmtime(&sunday);
  if (!sun_tm || strftime(buf, sizeof(buf), "%I:%M %p %U %W %Z", sun_tm) != 18 ||
      strcmp(buf, "12:00 AM 01 00 UTC") != 0) return 15;
  mon_tm = gmtime(&monday);
  if (!mon_tm || strftime(buf, sizeof(buf), "%I:%M %p %U %W %Z", mon_tm) != 18 ||
      strcmp(buf, "12:00 AM 01 01 UTC") != 0) return 16;
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
  nmk.tm_sec = 70;
  nmk.tm_min = 61;
  nmk.tm_hour = 25;
  nmk.tm_mday = 32;
  nmk.tm_mon = 0;
  nmk.tm_year = 70;
  nmk.tm_isdst = -1;
  if (mktime(&nmk) != 2772130 || nmk.tm_sec != 10 || nmk.tm_min != 2 || nmk.tm_hour != 2 ||
      nmk.tm_mday != 2 || nmk.tm_mon != 1 || nmk.tm_year != 70 ||
      nmk.tm_wday != 1 || nmk.tm_yday != 32 || nmk.tm_isdst != 0) return 20;
  btm = gmtime(&before_epoch);
  if (!btm || btm->tm_sec != 59 || btm->tm_min != 59 || btm->tm_hour != 23 ||
      btm->tm_mday != 31 || btm->tm_mon != 11 || btm->tm_year != 69 ||
      btm->tm_wday != 3 || btm->tm_yday != 364 || btm->tm_isdst != 0) return 12;
  if (strcmp(asctime(btm), "Wed Dec 31 23:59:59 1969\\n") != 0) return 13;
  if (strftime(buf, sizeof(buf), "%G %g %V %u", btm) != 12 ||
      strcmp(buf, "1970 70 01 3") != 0) return 18;
  bmk.tm_sec = 59;
  bmk.tm_min = 59;
  bmk.tm_hour = 23;
  bmk.tm_mday = 31;
  bmk.tm_mon = 11;
  bmk.tm_year = 69;
  bmk.tm_isdst = -1;
  if (mktime(&bmk) != -1 || bmk.tm_wday != 3 || bmk.tm_yday != 364 || bmk.tm_isdst != 0) return 14;
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
  if (strerror(5)[0] != 'e') return 5;
  if (strcmp(strerror(0), strerror(5)) == 0) return 6;
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

const linkedWideIOSource = await inlineStandardIncludes(`#include <errno.h>
#include <stdio.h>
#include <wchar.h>
int main(void) {
  FILE *fp = fopen("wide.txt", "w+");
  wchar_t line[8];
  wchar_t text[] = {'O', 0x3042, '\\n', 0};
  if (!fp) return 1;
  if (fwide(fp, 1) != 1 || fwide(fp, -1) != 1 || fwide(fp, 0) != 1) return 2;
  errno = 0;
  if (fwide((FILE *)3, 1) != 0 || errno != EBADF) return 18;
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

const linkedAssertFailSource = `
void __assert_rtn(char *func, char *file, int line, char *expr);
int main(void) {
  __assert_rtn("main", "assert_runtime_fail.c", 3, "0");
  return 42;
}
`;
const linkedAssertFail = await toolchain.instantiateLinkedWasm(linkedAssertFailSource, {
  exports: [
    "main",
    "__agc_runtime_termination_kind",
    "__agc_runtime_termination_status",
  ],
  useStdlib: true,
});
let linkedAssertTrapped = false;
try {
  linkedAssertFail.instance.exports.main();
} catch (_) {
  linkedAssertTrapped = true;
}
if (!linkedAssertTrapped) {
  throw new Error("linked runtime __assert_rtn() did not trap through abort");
}
if (Number(linkedAssertFail.instance.exports.__agc_runtime_termination_kind()) !== 2 ||
    Number(linkedAssertFail.instance.exports.__agc_runtime_termination_status()) !== 0) {
  throw new Error("linked runtime __assert_rtn() did not notify abort termination");
}

toolchain = await freshToolchain();

const linkedLongjmpFailSource = `
typedef long jmp_buf[48];
void longjmp(jmp_buf env, int val);
int main(void) {
  jmp_buf env;
  longjmp(env, 7);
  return 42;
}
`;
try {
  toolchain.compileLinkedWasm(linkedLongjmpFailSource, {
    exports: ["main"],
    useStdlib: true,
  });
  throw new Error("linked runtime longjmp() unexpectedly linked");
} catch (error) {
  if (error.message === "linked runtime longjmp() unexpectedly linked" ||
      !error.message.includes("unsupported C control-flow function")) {
    throw error;
  }
}

toolchain = await freshToolchain();

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

const jsSnprintfStringSource = `
int snprintf(char *buf, unsigned long size, const char *fmt, const char *s);
int main(void) {
  char a[8];
  char b[8];
  char c[8];
  char d[8];
  char raw[3];
  int n = snprintf(a, sizeof(a), "%5s", "\u3042");
  int m = snprintf(b, sizeof(b), "%.3s", "\u3042Z");
  int p = snprintf(c, sizeof(c), "%.3s", (void *)0);
  raw[0] = (char)0xe3;
  raw[1] = 'Q';
  raw[2] = 0;
  int q = snprintf(d, sizeof(d), "%4.1s", raw);
  if (n != 5 || a[0] != ' ' || a[1] != ' ' || (unsigned char)a[2] != 0xe3 ||
      (unsigned char)a[3] != 0x81 || (unsigned char)a[4] != 0x82 || a[5] != 0) return 1;
  if (m != 3 || (unsigned char)b[0] != 0xe3 || (unsigned char)b[1] != 0x81 ||
      (unsigned char)b[2] != 0x82 || b[3] != 0) return 2;
  if (p != 3 || c[0] != '(' || c[1] != 'n' || c[2] != 'u' || c[3] != 0) return 3;
  if (q != 4 || d[0] != ' ' || d[1] != ' ' || d[2] != ' ' ||
      (unsigned char)d[3] != 0xe3 || d[4] != 0) return 4;
  return 42;
}
`;
const jsSnprintfString = await toolchain.instantiateLinkedWasm(jsSnprintfStringSource, {
  exports: ["main"],
  useStdlib: false,
});
const jsSnprintfStringResult = jsSnprintfString.instance.exports.main();
if (jsSnprintfStringResult !== 42) {
  throw new Error(`JS stdio snprintf string width/precision imports failed: ${jsSnprintfStringResult}`);
}

const jsSnprintfCharSource = `
int snprintf(char *buf, unsigned long size, const char *fmt, int ch);
int main(void) {
  char a[8];
  char b[2];
  int n = snprintf(a, sizeof(a), "%3c", 0xe3);
  int m = snprintf(b, sizeof(b), "%c", 0xe3);
  if (n != 3 || a[0] != ' ' || a[1] != ' ' || (unsigned char)a[2] != 0xe3 || a[3] != 0) return 1;
  if (m != 1 || (unsigned char)b[0] != 0xe3 || b[1] != 0) return 2;
  return 42;
}
`;
const jsSnprintfChar = await toolchain.instantiateLinkedWasm(jsSnprintfCharSource, {
  exports: ["main"],
  useStdlib: false,
});
const jsSnprintfCharResult = jsSnprintfChar.instance.exports.main();
if (jsSnprintfCharResult !== 42) {
  throw new Error(`JS stdio snprintf char raw byte import failed: ${jsSnprintfCharResult}`);
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

toolchain = await freshToolchain();

const linkedMathClassSource = await inlineStandardIncludes(`#include <math.h>
#include <fenv.h>
int main(void) {
  double z = 0.0;
  double nanv = z / z;
  double infv = 1.0 / z;
  double nzero = -z;
  double subnormal = 1.0e-310;
  int expv = 0;
  int fexpv = 0;
  int lexpv = 0;
  int dquo = 0;
  int fquo = 0;
  int lquo = 0;
  int dquo_bits = 0;
  int dquo_neg_bits = 0;
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
  if ((int)(sqrt(2.0) * 1000.0) != 1414 || (int)(sqrtf(2.0f) * 1000.0f) != 1414) return 94;
  if ((int)(sqrtl(2.0L) * 1000.0L) != 1414) return 95;
  if (!isnan(sqrt(-1.0)) || !isnan(sqrtf(-1.0f)) || !isnan(sqrtl(-1.0L))) return 96;
  if (!isnan(sqrt(nanv)) || !signbit(sqrt(nzero)) || sqrt(infv) <= 1.0e300) return 97;
  if (sqrt(1.0e200) <= 9.9e99 || sqrt(1.0e200) >= 1.01e100) return 149;
  if (sqrt(1.0e-200) <= 9.9e-101 || sqrt(1.0e-200) >= 1.01e-100) return 150;
  if (sqrtf(1.0e20f) <= 9.9e9f || sqrtf(1.0e20f) >= 1.01e10f) return 151;
  if (!isnan(pow(-2.0, 0.5)) || !isnan(powf(-2.0f, 0.5f)) || !isnan(powl(-2.0L, 0.5L))) return 98;
  if (!isnan(pow(nanv, 2.0)) || (int)pow(nanv, 0.0) != 1) return 99;
  if (pow(z, -0.5) <= 1.0e300 || !signbit(pow(nzero, 3.0))) return 100;
  if (pow(nzero, -3.0) >= -1.0e300) return 101;
  if (pow(-2.0, 10000000000.0) <= 1.0e300 || pow(-2.0, 10000000001.0) >= -1.0e300) return 127;
  if (pow(2.0, -10000000000.0) != 0.0 || !signbit(pow(-2.0, -10000000001.0))) return 128;
  if (!signbit(pow(nzero, 10000000001.0)) || pow(nzero, -10000000001.0) >= -1.0e300) return 129;
  if (log(z) >= -1.0e300 || log(infv) <= 1.0e300) return 102;
  if (!isnan(log(-1.0)) || !isnan(logf(-1.0f)) || !isnan(logl(-1.0L))) return 103;
  if (!isnan(log(nanv)) || log2(z) >= -1.0e300 || !isnan(log2(-1.0))) return 104;
  if (log10(infv) <= 1.0e300 || !isnan(log10(-1.0))) return 105;
  if (log1p(-1.0) >= -1.0e300 || !isnan(log1p(-2.0))) return 106;
  if (log1p(1.0e-20) <= 0.0 || log1p(1.0e-20) >= 2.0e-20) return 155;
  if (log1p(-1.0e-20) >= 0.0 || log1p(-1.0e-20) <= -2.0e-20) return 156;
  if (exp(infv) <= 1.0e300 || exp(-infv) != 0.0) return 107;
  if (!isnan(exp(nanv)) || !isnan(expf((float)nanv)) || !isnan(expl((long double)nanv))) return 108;
  if (exp2(infv) <= 1.0e300 || exp2(-infv) != 0.0) return 109;
  if (expm1(infv) <= 1.0e300 || (int)expm1(-infv) != -1) return 110;
  if (!signbit(expm1(nzero)) || !signbit(expm1f(-0.0f)) || !signbit(expm1l(-0.0L))) return 139;
  if (exp(10000.0) <= 1.0e300 || exp(-10000.0) != 0.0) return 140;
  if (exp2(2000.0) <= 1.0e300 || exp2(-2000.0) != 0.0) return 141;
  if ((int)expm1(-10000.0) != -1) return 142;
  if (expm1(1.0e-20) <= 0.0 || expm1(1.0e-20) >= 2.0e-20) return 157;
  if (expm1(-1.0e-20) >= 0.0 || expm1(-1.0e-20) <= -2.0e-20) return 158;
  if (!isnan(sin(infv)) || !isnan(sinf((float)infv)) || !isnan(sinl((long double)infv))) return 111;
  if (!isnan(cos(infv)) || !isnan(cosf((float)infv)) || !isnan(cosl((long double)infv))) return 112;
  if (!isnan(tan(infv)) || !isnan(tanf((float)infv)) || !isnan(tanl((long double)infv))) return 113;
  if (sin(10000.0) != sin(10000.0) || sin(10000.0) <= -2.0 || sin(10000.0) >= 2.0) return 146;
  if (cos(10000.0) != cos(10000.0) || cos(10000.0) <= -2.0 || cos(10000.0) >= 2.0) return 147;
  if (tan(10000.0) != tan(10000.0) || tan(10000.0) <= -100.0 || tan(10000.0) >= 100.0) return 148;
  if (sinh(infv) <= 1.0e300 || sinh(-infv) >= -1.0e300) return 114;
  if (cosh(infv) <= 1.0e300 || cosh(-infv) <= 1.0e300) return 115;
  if ((int)tanh(infv) != 1 || (int)tanh(-infv) != -1) return 116;
  if (!signbit(sinh(nzero)) || !signbit(tanh(nzero)) || !signbit(asinh(nzero))) return 169;
  if (sinh(1.0e-20) <= 0.0 || sinh(1.0e-20) >= 2.0e-20) return 170;
  if (sinh(-1.0e-20) >= 0.0 || sinh(-1.0e-20) <= -2.0e-20) return 171;
  if (tanh(1.0e-20) <= 0.0 || tanh(1.0e-20) >= 2.0e-20) return 172;
  if (tanh(-1.0e-20) >= 0.0 || tanh(-1.0e-20) <= -2.0e-20) return 173;
  if (sinh(10000.0) <= 1.0e300 || sinh(-10000.0) >= -1.0e300) return 143;
  if (cosh(10000.0) <= 1.0e300 || cosh(-10000.0) <= 1.0e300) return 144;
  if ((int)tanh(10000.0) != 1 || (int)tanh(-10000.0) != -1) return 145;
  if (acosh(infv) <= 1.0e300 || !isnan(acosh(0.5)) || !isnan(acoshf(0.5f))) return 117;
  if (atanh(1.0) <= 1.0e300 || atanh(-1.0) >= -1.0e300) return 118;
  if (!isnan(atanh(2.0)) || !isnan(atanhl(2.0L))) return 119;
  if (!signbit(atanh(nzero))) return 159;
  if (atanh(1.0e-20) <= 0.0 || atanh(1.0e-20) >= 2.0e-20) return 160;
  if (atanh(-1.0e-20) >= 0.0 || atanh(-1.0e-20) <= -2.0e-20) return 161;
  if ((int)(atan(1.0) * 1000.0) < 783 || (int)(atan(1.0) * 1000.0) > 787) return 162;
  if ((int)(atan(-1.0) * 1000.0) > -783 || (int)(atan(-1.0) * 1000.0) < -787) return 163;
  if (!signbit(atan(nzero))) return 164;
  if (atan(infv) <= 1.56 || atan(infv) >= 1.58 || atan(-infv) >= -1.56 || atan(-infv) <= -1.58) return 165;
  if (asinh(1.0e200) <= 400.0 || asinh(1.0e200) >= 500.0) return 123;
  if (asinh(-1.0e200) >= -400.0 || asinh(-1.0e200) <= -500.0) return 124;
  if (asinh(1.0e-20) <= 0.0 || asinh(1.0e-20) >= 2.0e-20) return 174;
  if (asinh(-1.0e-20) >= 0.0 || asinh(-1.0e-20) <= -2.0e-20) return 175;
  if (acosh(1.0e200) <= 400.0 || acosh(1.0e200) >= 500.0) return 125;
  if (acoshl(1.0e200L) <= 400.0L || acoshl(1.0e200L) >= 500.0L) return 126;
  if (cbrt(infv) <= 1.0e300 || cbrt(-infv) >= -1.0e300) return 120;
  if (!isnan(cbrt(nanv)) || !isnan(cbrtf((float)nanv))) return 121;
  if (cbrtl((long double)infv) <= 1.0e300L || !signbit(cbrt(nzero))) return 122;
  if (cbrt(1.0e300) <= 9.9e99 || cbrt(1.0e300) >= 1.01e100) return 152;
  if (cbrt(-1.0e300) >= -9.9e99 || cbrt(-1.0e300) <= -1.01e100) return 153;
  if (cbrt(1.0e-300) <= 9.9e-101 || cbrt(1.0e-300) >= 1.01e-100) return 154;
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
  if ((int)(scalbn(0.75, 4) * 1000.0) != 12000) return 21;
  if ((int)(scalbnf(0.5f, 5) * 1000.0f) != 16000) return 22;
  if ((int)(scalbnl(0.25L, 6) * 1000.0L) != 16000) return 23;
  if ((int)(scalbln(1.5, 3L) * 1000.0) != 12000) return 24;
  if ((int)(scalblnf(1.25f, 2L) * 1000.0f) != 5000) return 25;
  if ((int)(scalblnl(3.0L, -1L) * 1000.0L) != 1500) return 26;
  if (ldexp(1.0, 5000) <= 1.0e300 || !signbit(ldexp(-1.0, -5000))) return 127;
  if (scalbln(1.0, 5000L) <= 1.0e300 || !signbit(scalbln(-1.0, -5000L))) return 128;
  if (scalblnf(1.0f, 5000L) <= 1.0e30f) return 129;
  if (!signbit(scalblnl(-1.0L, -5000L))) return 130;
  if (ilogb(8.0) != 3 || ilogbf(0.75f) != -1 || ilogbl(0.25L) != -2) return 27;
  if ((int)logb(8.0) != 3 || (int)logbf(0.75f) != -1 || (int)logbl(0.25L) != -2) return 28;
  if ((int)(fmod(7.5, 2.0) * 1000.0) != 1500 || (int)(fmodf(7.5f, 2.0f) * 1000.0f) != 1500) return 29;
  if ((int)(fmodl(7.5L, 2.0L) * 1000.0L) != 1500) return 30;
  if (!isnan(fmod(7.5, z)) || !isnan(fmod(infv, 2.0))) return 31;
  if ((int)fmod(7.5, infv) != 7 || !signbit(fmod(-z, 3.0))) return 32;
  if (fmod(1.0e20, 3.0) < 0.0 || fmod(1.0e20, 3.0) >= 3.0) return 133;
  if (fmod(-1.0e20, 3.0) > 0.0 || fmod(-1.0e20, 3.0) <= -3.0) return 134;
  if (fmodl(1.0e20L, 3.0L) < 0.0L || fmodl(1.0e20L, 3.0L) >= 3.0L) return 135;
  if ((int)(hypot(3.0, 4.0) * 1000.0) != 5000 || (int)(hypotf(3.0f, 4.0f) * 1000.0f) != 5000) return 33;
  if ((int)(hypotl(3.0L, 4.0L) * 1000.0L) != 5000) return 34;
  if (hypot(1.0e200, 1.0e200) <= 1.0e200) return 35;
  if (hypot(infv, nanv) <= 1.0e300 || !isnan(hypot(nanv, 3.0))) return 36;
  if (!signbit(atan2(-z, z))) return 92;
  if ((int)(atan2(z, -z) * 1000.0) < 3140 || (int)(atan2(z, -z) * 1000.0) > 3143) return 93;
  if ((int)(atan2(infv, infv) * 1000.0) < 783 || (int)(atan2(infv, infv) * 1000.0) > 787) return 94;
  if ((int)(atan2(infv, -infv) * 1000.0) < 2354 || (int)(atan2(infv, -infv) * 1000.0) > 2358) return 95;
  if ((int)(modf(-3.75, &ip) * 100.0) != -75 || (int)ip != -3) return 37;
  if ((int)(modff(2.25f, &fip) * 100.0f) != 25 || (int)fip != 2) return 38;
  if ((int)(modfl(5.5L, &lip) * 100.0L) != 50 || (int)lip != 5) return 39;
  if (!signbit(modf(nzero, &ip)) || !signbit(ip)) return 141;
  if (!signbit(modff(-2.0f, &fip)) || (int)fip != -2) return 142;
  if (!signbit(modfl(-0.0L, &lip)) || !signbit(lip)) return 143;
  if ((int)copysign(2.0, nzero) != -2 || !signbit(copysign(2.0, nzero))) return 40;
  if ((int)copysignf(2.0f, -0.0f) != -2 || !signbit(copysignf(2.0f, -0.0f))) return 41;
  if ((int)copysignl(2.0L, -0.0L) != -2 || !signbit(copysignl(2.0L, -0.0L))) return 42;
  if (signbit(fabs(nzero)) || signbit(fabsf(-0.0f)) || signbit(fabsl(-0.0L))) return 131;
  if (signbit(copysign(nzero, 1.0)) || signbit(copysignf(-0.0f, 1.0f)) ||
      signbit(copysignl(-0.0L, 1.0L))) return 132;
  if (!isnan(nan("")) || !isnan(nanf("")) || !isnan(nanl(""))) return 43;
  if ((int)(fdim(5.5, 2.0) * 1000.0) != 3500 || (int)(fdim(2.0, 5.5) * 1000.0) != 0) return 44;
  if ((int)(fdimf(5.5f, 2.0f) * 1000.0f) != 3500) return 45;
  if ((int)(fdiml(5.5L, 2.0L) * 1000.0L) != 3500) return 46;
  if ((int)(fma(2.0, 3.0, 0.5) * 1000.0) != 6500) return 47;
  if ((int)(fmaf(2.0f, 3.0f, 0.5f) * 1000.0f) != 6500) return 48;
  if ((int)(fmal(2.0L, 3.0L, 0.5L) * 1000.0L) != 6500) return 49;
  if ((int)(remainder(5.5, 2.0) * 1000.0) != -500) return 50;
  if ((int)(remainderf(5.5f, 2.0f) * 1000.0f) != -500) return 43;
  if ((int)(remainderl(5.5L, 2.0L) * 1000.0L) != -500) return 44;
  if ((int)(remquo(5.5, 2.0, &dquo) * 1000.0) != -500 || dquo != 3) return 45;
  if ((int)(remquof(5.5f, 2.0f, &fquo) * 1000.0f) != -500 || fquo != 3) return 46;
  if ((int)(remquol(5.5L, 2.0L, &lquo) * 1000.0L) != -500 || lquo != 3) return 47;
  if ((int)(remquo(19.5, 2.0, &dquo_bits) * 1000.0) != -500 || dquo_bits != 2) return 48;
  if ((int)(remquo(-19.5, 2.0, &dquo_neg_bits) * 1000.0) != 500 || dquo_neg_bits != -2) return 49;
  if (remainder(1.0e20, 3.0) < -1.5 || remainder(1.0e20, 3.0) > 1.5) return 136;
  if (remquo(1.0e20, 3.0, &dquo_bits) < -1.5 || remquo(1.0e20, 3.0, &dquo_bits) > 1.5 ||
      dquo_bits < -7 || dquo_bits > 7) return 137;
  if (remquo(-1.0e20, 3.0, &dquo_neg_bits) < -1.5 || remquo(-1.0e20, 3.0, &dquo_neg_bits) > 1.5 ||
      dquo_neg_bits < -7 || dquo_neg_bits > 7) return 138;
  if ((int)(exp2(3.0) * 1000.0) < 7998 || (int)(exp2(3.0) * 1000.0) > 8002) return 43;
  if ((int)(exp2f(3.0f) * 1000.0f) < 7998 || (int)(exp2f(3.0f) * 1000.0f) > 8002) return 44;
  if ((int)(exp2l(3.0L) * 1000.0L) < 7998 || (int)(exp2l(3.0L) * 1000.0L) > 8002) return 45;
  if ((int)(expm1(1.0) * 1000.0) < 1716 || (int)(expm1(1.0) * 1000.0) > 1720) return 46;
  if ((int)(expm1f(1.0f) * 1000.0f) < 1716 || (int)(expm1f(1.0f) * 1000.0f) > 1720) return 47;
  if ((int)(expm1l(1.0L) * 1000.0L) < 1716 || (int)(expm1l(1.0L) * 1000.0L) > 1720) return 48;
  if ((int)(log1p(1.0) * 1000.0) < 691 || (int)(log1p(1.0) * 1000.0) > 695) return 49;
  if ((int)(log1pf(1.0f) * 1000.0f) < 691 || (int)(log1pf(1.0f) * 1000.0f) > 695) return 50;
  if ((int)(log1pl(1.0L) * 1000.0L) < 691 || (int)(log1pl(1.0L) * 1000.0L) > 695) return 51;
  if (!signbit(log1p(nzero)) || !signbit(log1pf(-0.0f)) || !signbit(log1pl(-0.0L))) return 140;
  if ((int)(sinh(1.0) * 1000.0) < 1174 || (int)(sinh(1.0) * 1000.0) > 1176) return 52;
  if ((int)(sinhf(1.0f) * 1000.0f) < 1174 || (int)(sinhf(1.0f) * 1000.0f) > 1176) return 53;
  if ((int)(sinhl(1.0L) * 1000.0L) < 1174 || (int)(sinhl(1.0L) * 1000.0L) > 1176) return 54;
  if ((int)(cosh(1.0) * 1000.0) < 1542 || (int)(cosh(1.0) * 1000.0) > 1544) return 55;
  if ((int)(coshf(1.0f) * 1000.0f) < 1542 || (int)(coshf(1.0f) * 1000.0f) > 1544) return 56;
  if ((int)(coshl(1.0L) * 1000.0L) < 1542 || (int)(coshl(1.0L) * 1000.0L) > 1544) return 57;
  if ((int)(tanh(1.0) * 1000.0) < 760 || (int)(tanh(1.0) * 1000.0) > 762) return 58;
  if ((int)(tanhf(1.0f) * 1000.0f) < 760 || (int)(tanhf(1.0f) * 1000.0f) > 762) return 59;
  if ((int)(tanhl(1.0L) * 1000.0L) < 760 || (int)(tanhl(1.0L) * 1000.0L) > 762) return 60;
  if ((int)(asinh(1.0) * 1000.0) < 880 || (int)(asinh(1.0) * 1000.0) > 882) return 61;
  if ((int)(asinhf(1.0f) * 1000.0f) < 880 || (int)(asinhf(1.0f) * 1000.0f) > 882) return 62;
  if ((int)(asinhl(1.0L) * 1000.0L) < 880 || (int)(asinhl(1.0L) * 1000.0L) > 882) return 63;
  if ((int)(acosh(2.0) * 1000.0) < 1315 || (int)(acosh(2.0) * 1000.0) > 1317) return 64;
  if ((int)(acoshf(2.0f) * 1000.0f) < 1315 || (int)(acoshf(2.0f) * 1000.0f) > 1317) return 65;
  if ((int)(acoshl(2.0L) * 1000.0L) < 1315 || (int)(acoshl(2.0L) * 1000.0L) > 1317) return 66;
  if ((int)(asin(1.0) * 1000.0) < 1568 || (int)(asin(1.0) * 1000.0) > 1572) return 90;
  if ((int)(asin(-1.0) * 1000.0) > -1568 || (int)(asin(-1.0) * 1000.0) < -1572) return 182;
  if (!signbit(asin(nzero))) return 183;
  if (!isnan(asin(2.0)) || !isnan(asinf(2.0f)) || !isnan(asinl(2.0L))) return 91;
  if ((int)(acos(0.0) * 1000.0) < 1568 || (int)(acos(0.0) * 1000.0) > 1572) return 92;
  if (acos(1.0) != 0.0 || signbit(acos(1.0))) return 184;
  if ((int)(acos(-1.0) * 1000.0) < 3140 || (int)(acos(-1.0) * 1000.0) > 3143) return 185;
  if (!isnan(acos(2.0)) || !isnan(acosf(2.0f)) || !isnan(acosl(2.0L))) return 93;
  if ((int)(atanh(0.5) * 1000.0) < 548 || (int)(atanh(0.5) * 1000.0) > 550) return 67;
  if ((int)(atanhf(0.5f) * 1000.0f) < 548 || (int)(atanhf(0.5f) * 1000.0f) > 550) return 68;
  if ((int)(atanhl(0.5L) * 1000.0L) < 548 || (int)(atanhl(0.5L) * 1000.0L) > 550) return 69;
  if ((int)(erf(1.0) * 1000.0) < 841 || (int)(erf(1.0) * 1000.0) > 844) return 70;
  if ((int)(erff(1.0f) * 1000.0f) < 841 || (int)(erff(1.0f) * 1000.0f) > 844) return 71;
  if ((int)(erfl(1.0L) * 1000.0L) < 841 || (int)(erfl(1.0L) * 1000.0L) > 844) return 72;
  if (erf(z) != 0.0 || !signbit(erf(nzero))) return 176;
  if (erff(0.0f) != 0.0f || !signbit(erff(-0.0f))) return 177;
  if (erfl(0.0L) != 0.0L || !signbit(erfl(-0.0L))) return 178;
  if ((int)(erfc(1.0) * 1000.0) < 156 || (int)(erfc(1.0) * 1000.0) > 158) return 73;
  if ((int)(erfcf(1.0f) * 1000.0f) < 156 || (int)(erfcf(1.0f) * 1000.0f) > 158) return 74;
  if ((int)(erfcl(1.0L) * 1000.0L) < 156 || (int)(erfcl(1.0L) * 1000.0L) > 158) return 75;
  if (erfc(z) != 1.0 || erfc(nzero) != 1.0) return 179;
  if (erfcf(0.0f) != 1.0f || erfcf(-0.0f) != 1.0f) return 180;
  if (erfcl(0.0L) != 1.0L || erfcl(-0.0L) != 1.0L) return 181;
  if (erfc(10.0) <= 0.0 || erfc(10.0) >= 1.0e-40) return 166;
  if (erfcf(5.0f) <= 0.0f || erfcf(5.0f) >= 1.0e-10f) return 167;
  if (erfcl(10.0L) <= 0.0L || erfcl(10.0L) >= 1.0e-40L) return 168;
  if (nearbyint(2.5) != 2.0 || nearbyintf(-2.5f) != -2.0f || nearbyintl(3.5L) != 4.0L) return 76;
  if (lround(2.5) != 3 || lroundf(-2.5f) != -3 || lroundl(3.5L) != 4) return 77;
  if (llround(-3.5) != -4 || llroundf(2.5f) != 3 || llroundl(-2.5L) != -3) return 78;
  if (!isnan(floor(nanv)) || ceil(infv) <= 1.0e300 || round(-infv) >= -1.0e300) return 96;
  if (!signbit(floor(-z)) || !signbit(trunc(-0.8)) || !signbit(ceil(-0.8)) || !signbit(round(-0.3))) return 97;
  if (!isnan(floorf((float)nanv)) || ceill((long double)infv) <= 1.0e300L) return 98;
  if (!signbit(roundl(-0.3L))) return 99;
  if (trunc(10000000000.75) != 10000000000.0 || floor(-10000000000.75) != -10000000001.0) return 122;
  if (ceil(10000000000.25) != 10000000001.0 || round(-10000000000.25) != -10000000000.0) return 123;
  if (trunc(1.0e20) != 1.0e20 || floor(1.0e20) != 1.0e20) return 124;
  if (ceil(-1.0e20) != -1.0e20 || round(-1.0e20) != -1.0e20) return 125;
  if (nearbyint(10000000000.5) != 10000000000.0 || rint(10000000001.5) != 10000000002.0) return 126;
  if (fesetround(FE_UPWARD) != 0 || rint(2.1) != 3.0 || rintf(-2.1f) != -2.0f) return 79;
  if (lrint(2.1) != 3 || llrintf(-2.1f) != -2) return 80;
  if (fesetround(FE_DOWNWARD) != 0 || rintl(2.9L) != 2.0L || nearbyint(-2.1) != -3.0) return 81;
  if (lrintl(2.9L) != 2 || llrintl(-2.1L) != -3) return 82;
  if (fesetround(FE_TOWARDZERO) != 0 || rint(2.9) != 2.0 || rint(-2.9) != -2.0) return 83;
  if (lrintf(2.9f) != 2 || llrint(-2.9) != -2) return 84;
  if (fesetround(FE_TONEAREST) != 0 || rint(2.5) != 2.0 || rint(3.5) != 4.0 || lrint(3.5) != 4) return 85;
  if ((int)fmin(nanv, 7.0) != 7 || (int)fmin(7.0, nanv) != 7) return 86;
  if ((int)fminf((float)nanv, 5.0f) != 5 || (int)fminl(6.0L, (long double)nanv) != 6) return 87;
  if ((int)fmax(nanv, 7.0) != 7 || (int)fmax(7.0, nanv) != 7) return 88;
  if ((int)fmaxf((float)nanv, 5.0f) != 5 || (int)fmaxl(6.0L, (long double)nanv) != 6) return 89;
  if (!signbit(fmin(-z, z)) || !signbit(fmin(z, -z))) return 90;
  if (signbit(fmax(-z, z)) || signbit(fmax(z, -z))) return 91;
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
int printf(const char *fmt, ...);
int puts(const char *s);
int fputs(const char *s, void *stream);
int fputc(int c, void *stream);
int fprintf(void *stream, const char *fmt, ...);
int fflush(void *stream);
int fclose(void *stream);
unsigned long fwrite(const void *ptr, unsigned long size, unsigned long nmemb, void *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, void *stream);
long write(int fd, const void *buf, unsigned long count);
long lseek(int fd, long offset, int whence);
void *fopen(const char *path, const char *mode);
int *__error(void);
int ferror(void *stream);
void clearerr(void *stream);
void perror(const char *s);
#define errno (*__error())
#define ENOENT 2
#define EBADF 9
#define ENAMETOOLONG 36
#define EINVAL 22
int main(void) {
  char buf[4];
  char long_path[80];
  char raw_utf8[4];
  raw_utf8[0] = (char)0xe3;
  raw_utf8[1] = (char)0x81;
  raw_utf8[2] = (char)0x82;
  raw_utf8[3] = 0;
  if (fputs("A", (void *)1) != 1) return 1;
  if (fputc('B', (void *)1) != 'B') return 2;
  if (fputs("E", (void *)2) != 1) return 3;
  if (fputc('R', (void *)2) != 'R') return 4;
  if (fflush((void *)1) != 0) return 5;
  errno = 0;
  if (fflush((void *)3) != -1 || errno != EBADF) return 21;
  if (fclose((void *)0) != 0 || fclose((void *)1) != 0 || fclose((void *)2) != 0) return 22;
  errno = 0;
  if (fclose((void *)3) != -1 || errno != EBADF) return 23;
  if (fwrite("CD", 1, 2, (void *)1) != 2) return 6;
  if (fwrite("!", 1, 1, (void *)2) != 1) return 7;
  errno = 0;
  if (fputs("q", (void *)0) != -1 || errno != EBADF) return 8;
  if (ferror((void *)0) != 1) return 31;
  clearerr((void *)0);
  if (ferror((void *)0) != 0) return 32;
  errno = 0;
  if (fputc('r', (void *)0) != -1 || errno != EBADF) return 9;
  errno = 0;
  if (fprintf((void *)0, "t%d", 1) != -1 || errno != EBADF) return 24;
  errno = 0;
  if (fwrite("s", 1, 1, (void *)0) != 0 || errno != EBADF) return 10;
  errno = 0;
  if (fwrite("z", 0, 1, (void *)0) != 0 || errno != 0) return 25;
  errno = 0;
  if (fwrite("z", (unsigned long)-1, 1, (void *)1) != 0 || errno != EINVAL) return 26;
  errno = 0;
  if (fread(buf, (unsigned long)-1, 1, (void *)0) != 0 || errno != EINVAL) return 27;
  errno = 0;
  if (fread(buf, (unsigned long)-1, 1, (void *)1) != 0 || errno != EBADF) return 34;
  if (printf("\u3042") != 3) return 35;
  if (fputs("\u3042", (void *)1) != 3) return 36;
  if (puts("\u3042") != 4) return 37;
  if (fprintf((void *)2, "\u3042") != 3) return 38;
  if (fputc(0xe3, (void *)1) != 0xe3) return 39;
  if (fputs(raw_utf8, (void *)1) != 3) return 44;
  if (puts(raw_utf8) != 4) return 45;
  if (fwrite(raw_utf8, 1, 3, (void *)1) != 3) return 46;
  if (write(1, raw_utf8, 3) != 3) return 47;
  if (write(1, "W", 1) != 1) return 11;
  if (write(2, "e", 1) != 1) return 12;
  errno = 0;
  if (write(1, "x", (unsigned long)-1) != -1 || errno != EINVAL) return 30;
  errno = 0;
  if (write(0, "x", (unsigned long)-1) != -1 || errno != EBADF) return 33;
  errno = 0;
  if (write(0, "n", 1) != -1) return 13;
  if (errno != EBADF) return 16;
  perror("js");
  errno = EBADF;
  perror(raw_utf8);
  errno = 0;
  if (lseek(1, 0, 0) != -1) return 14;
  if (errno != EBADF) return 17;
  errno = 0;
  if (fopen((void *)0, "r") != 0 || errno != EINVAL) return 18;
  errno = 0;
  if (fopen((void *)-1, "r") != 0 || errno != EINVAL) return 29;
  errno = 0;
  if (fopen("missing.txt", "r") != 0 || errno != ENOENT) return 19;
  for (int i = 0; i < 70; i++) long_path[i] = 'a';
  long_path[70] = 0;
  errno = 0;
  if (fopen(long_path, "r") != 0 || errno != ENAMETOOLONG) return 28;
  errno = 0;
  if (fread(buf, 1, 1, (void *)1) != 0 || errno != EBADF) return 20;
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
if (jsBasicStdioResult !== 42 || jsBasicStdout !== "ABCDあああ\n�ああ\nああW" || jsBasicStderr !== "ER!あejs: error\nあ: error\n") {
  throw new Error(
    `JS basic stdio imports failed: result=${jsBasicStdioResult}, stdout=${JSON.stringify(jsBasicStdout)}, stderr=${JSON.stringify(jsBasicStderr)}`,
  );
}

const jsPrintfStringSource = `
int printf(const char *fmt, const char *s);
int main(void) {
  if (printf("%5s", "\u3042") != 5) return 1;
  if (printf("%.3s", "\u3042Z") != 3) return 2;
  if (printf("%s", (void *)0) != 6) return 3;
  if (printf("%.3s", (void *)0) != 3) return 4;
  return 42;
}
`;
let jsPrintfStringStdout = "";
const jsPrintfString = await toolchain.instantiateLinkedWasm(jsPrintfStringSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfStringStdout += chunk; },
});
const jsPrintfStringResult = jsPrintfString.instance.exports.main();
if (jsPrintfStringResult !== 42 || jsPrintfStringStdout !== "  ああ(null)(nu") {
  throw new Error(
    `JS printf string width/precision import failed: result=${jsPrintfStringResult}, stdout=${JSON.stringify(jsPrintfStringStdout)}`,
  );
}

const jsPrintfCharSource = `
int printf(const char *fmt, int ch, int *out);
int main(void) {
  int n = -1;
  if (printf("%3c%n", 0xe3, &n) != 3) return 1;
  if (n != 3) return 2;
  return 42;
}
`;
let jsPrintfCharStdout = "";
const jsPrintfChar = await toolchain.instantiateLinkedWasm(jsPrintfCharSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfCharStdout += chunk; },
});
const jsPrintfCharResult = jsPrintfChar.instance.exports.main();
if (jsPrintfCharResult !== 42 || jsPrintfCharStdout !== "  �") {
  throw new Error(
    `JS printf char raw byte import failed: result=${jsPrintfCharResult}, stdout=${JSON.stringify(jsPrintfCharStdout)}`,
  );
}

const jsPrintfCharSequenceSource = `
int printf(const char *fmt, int a, int b, int c);
int main(void) {
  if (printf("%c%c%c", 0xe3, 0x81, 0x82) != 3) return 1;
  return 42;
}
`;
let jsPrintfCharSequenceStdout = "";
const jsPrintfCharSequence = await toolchain.instantiateLinkedWasm(jsPrintfCharSequenceSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfCharSequenceStdout += chunk; },
});
const jsPrintfCharSequenceResult = jsPrintfCharSequence.instance.exports.main();
if (jsPrintfCharSequenceResult !== 42 || jsPrintfCharSequenceStdout !== "あ") {
  throw new Error(
    `JS printf char sequence import failed: result=${jsPrintfCharSequenceResult}, stdout=${JSON.stringify(jsPrintfCharSequenceStdout)}`,
  );
}

const jsPrintfRawStringSource = `
int printf(const char *fmt, const char *s, int *out);
int main(void) {
  char raw[3];
  int n = -1;
  raw[0] = (char)0xe3;
  raw[1] = 'Q';
  raw[2] = 0;
  if (printf("%4.1s%n", raw, &n) != 4) return 1;
  if (n != 4) return 2;
  return 42;
}
`;
let jsPrintfRawStringStdout = "";
const jsPrintfRawString = await toolchain.instantiateLinkedWasm(jsPrintfRawStringSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfRawStringStdout += chunk; },
});
const jsPrintfRawStringResult = jsPrintfRawString.instance.exports.main();
if (jsPrintfRawStringResult !== 42 || jsPrintfRawStringStdout !== "   �") {
  throw new Error(
    `JS printf raw string byte import failed: result=${jsPrintfRawStringResult}, stdout=${JSON.stringify(jsPrintfRawStringStdout)}`,
  );
}

const jsPrintfRawFormatLiteralSource = `
int printf(const char *fmt, const char *s);
int main(void) {
  char fmt[4];
  fmt[0] = (char)0xe3;
  fmt[1] = '%';
  fmt[2] = 's';
  fmt[3] = 0;
  if (printf(fmt, "Q") != 2) return 1;
  return 42;
}
`;
let jsPrintfRawFormatLiteralStdout = "";
const jsPrintfRawFormatLiteral = await toolchain.instantiateLinkedWasm(jsPrintfRawFormatLiteralSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfRawFormatLiteralStdout += chunk; },
});
const jsPrintfRawFormatLiteralResult = jsPrintfRawFormatLiteral.instance.exports.main();
if (jsPrintfRawFormatLiteralResult !== 42 || jsPrintfRawFormatLiteralStdout !== "�Q") {
  throw new Error(
    `JS printf raw format literal import failed: result=${jsPrintfRawFormatLiteralResult}, stdout=${JSON.stringify(jsPrintfRawFormatLiteralStdout)}`,
  );
}

const jsPrintfLongIntegerSource = `
int printf(const char *fmt, long a, unsigned long b, unsigned long c);
int main(void) {
  long a = (1L << 32) + 42;
  unsigned long b = (1UL << 32) + 15;
  if (printf("%ld:%lu:%lx", a, b, b) != 31) return 1;
  return 42;
}
`;
let jsPrintfLongIntegerStdout = "";
const jsPrintfLongInteger = await toolchain.instantiateLinkedWasm(jsPrintfLongIntegerSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfLongIntegerStdout += chunk; },
});
const jsPrintfLongIntegerResult = jsPrintfLongInteger.instance.exports.main();
if (jsPrintfLongIntegerResult !== 42 || jsPrintfLongIntegerStdout !== "4294967338:4294967311:10000000f") {
  throw new Error(
    `JS printf long integer import failed: result=${jsPrintfLongIntegerResult}, stdout=${JSON.stringify(jsPrintfLongIntegerStdout)}`,
  );
}

const jsPrintfIntegerPrecisionSource = `
int printf(const char *fmt, int a, int b, int c, int d, int e, int f, int g);
int main(void) {
  if (printf("%+.3d:% .3d:%.0d:%#.0o:%#.4x:%05.3d:%#.5o", 42, 7, 0, 0, 15, 7, 9) != 31) return 1;
  return 42;
}
`;
let jsPrintfIntegerPrecisionStdout = "";
const jsPrintfIntegerPrecision = await toolchain.instantiateLinkedWasm(jsPrintfIntegerPrecisionSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfIntegerPrecisionStdout += chunk; },
});
const jsPrintfIntegerPrecisionResult = jsPrintfIntegerPrecision.instance.exports.main();
if (jsPrintfIntegerPrecisionResult !== 42 || jsPrintfIntegerPrecisionStdout !== "+042: 007::0:0x000f:  007:00011") {
  throw new Error(
    `JS printf integer precision import failed: result=${jsPrintfIntegerPrecisionResult}, stdout=${JSON.stringify(jsPrintfIntegerPrecisionStdout)}`,
  );
}

const jsPrintfZeroPadPrefixSource = `
int printf(const char *fmt, int a, int b, int c, int d);
int main(void) {
  if (printf("%#08x:%#08X:%+05d:% 05d", 15, 15, 7, 7) != 29) return 1;
  return 42;
}
`;
let jsPrintfZeroPadPrefixStdout = "";
const jsPrintfZeroPadPrefix = await toolchain.instantiateLinkedWasm(jsPrintfZeroPadPrefixSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfZeroPadPrefixStdout += chunk; },
});
const jsPrintfZeroPadPrefixResult = jsPrintfZeroPadPrefix.instance.exports.main();
if (jsPrintfZeroPadPrefixResult !== 42 || jsPrintfZeroPadPrefixStdout !== "0x00000f:0X00000F:+0007: 0007") {
  throw new Error(
    `JS printf zero-pad prefix import failed: result=${jsPrintfZeroPadPrefixResult}, stdout=${JSON.stringify(jsPrintfZeroPadPrefixStdout)}`,
  );
}

const jsPrintfPointerSource = `
int printf(const char *fmt, unsigned long p);
int main(void) {
  unsigned long p = (1UL << 32) + 0x1234UL;
  if (printf("%p", p) != 11) return 1;
  return 42;
}
`;
let jsPrintfPointerStdout = "";
const jsPrintfPointer = await toolchain.instantiateLinkedWasm(jsPrintfPointerSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfPointerStdout += chunk; },
});
const jsPrintfPointerResult = jsPrintfPointer.instance.exports.main();
if (jsPrintfPointerResult !== 42 || jsPrintfPointerStdout !== "0x100001234") {
  throw new Error(
    `JS printf pointer import failed: result=${jsPrintfPointerResult}, stdout=${JSON.stringify(jsPrintfPointerStdout)}`,
  );
}

const jsPrintfCountStoreSource = `
int printf(const char *fmt, int *a, long *b, signed char *c, short *d);
int main(void) {
  int a = -1;
  long b = -1;
  signed char c = -1;
  short d = -1;
  if (printf("A%n\u3042%lnB%hhnC%hn", &a, &b, &c, &d) != 6) return 1;
  if (a != 1 || b != 4 || c != 5 || d != 6) return 2;
  return 42;
}
`;
let jsPrintfCountStoreStdout = "";
const jsPrintfCountStore = await toolchain.instantiateLinkedWasm(jsPrintfCountStoreSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfCountStoreStdout += chunk; },
});
const jsPrintfCountStoreResult = jsPrintfCountStore.instance.exports.main();
if (jsPrintfCountStoreResult !== 42 || jsPrintfCountStoreStdout !== "AあBC") {
  throw new Error(
    `JS printf count-store import failed: result=${jsPrintfCountStoreResult}, stdout=${JSON.stringify(jsPrintfCountStoreStdout)}`,
  );
}

const jsPrintfFloatSource = `
int printf(const char *fmt, double a, double b, double c, double d, double e, double f);
int main(void) {
  double zero = 0.0;
  double negzero = -zero;
  double inf = 1.0 / zero;
  double nanv = zero / zero;
  if (printf("%6.1f:%06.1f:%6f:%F:%f", 3.14, -2.34, inf, -inf, nanv, 0.0) != 29) return 1;
  if (printf(":%.2e:%10.1E:%010.1e", 1234.0, -0.0123, 9.99, 0.0, 0.0, 0.0) != 31) return 2;
  if (printf(":%.4g:%.3g:%8.2G:%#.0f:%#.0e:%#.3g",
             1234.0, 0.0001234, 12345.0, 3.0, 12.0, 123.0) != 38) return 3;
  if (printf(":%+.1f:% .1f:%+08.1f:%.1f:%.1e:%.1g",
             3.14, 3.14, 3.14, negzero, negzero, negzero) != 36) return 4;
  if (printf(":%.1a:%.1A:%08.0a:%#.0a", 3.0, -0.5, 1.0, 1.0, 0.0, 0.0) != 36) return 5;
  return 42;
}
`;
let jsPrintfFloatStdout = "";
const jsPrintfFloat = await toolchain.instantiateLinkedWasm(jsPrintfFloatSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsPrintfFloatStdout += chunk; },
});
const jsPrintfFloatResult = jsPrintfFloat.instance.exports.main();
if (jsPrintfFloatResult !== 42 ||
    jsPrintfFloatStdout !== "   3.1:-002.3:   inf:-INF:nan:1.23e+03:  -1.2E-02:0001.0e+01:1234:0.000123: 1.2E+04:3.:1.e+01:123.:+3.1: 3.1:+00003.1:-0.0:-0.0e+00:-0:0x1.8p+1:-0X1.0P-1:000x1p+0:0x1.p+0") {
  throw new Error(
    `JS printf float import failed: result=${jsPrintfFloatResult}, stdout=${JSON.stringify(jsPrintfFloatStdout)}`,
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
int ungetc(int c, void *stream);
int *__error(void);
void perror(const char *s);
#define errno (*__error())
#define EBADF 9
#define EINVAL 22
int main(void) {
  char line[4];
  char rest[4];
  errno = 0;
  if (fgetc((void *)1) != -1 || errno != EBADF) return 11;
  if (ferror((void *)1) != 0) return 18;
  errno = 0;
  if (fgets(line, sizeof(line), (void *)1) != 0 || errno != EBADF) return 12;
  errno = 0;
  if (fgets(line, 0, (void *)1) != 0 || errno != EBADF) return 20;
  errno = 0;
  if (fgets(line, 0, (void *)0) != 0 || errno != EINVAL || ferror((void *)0)) return 21;
  errno = 0;
  if (fread(rest, 1, 1, (void *)1) != 0 || errno != EBADF) return 13;
  errno = 0;
  if (ungetc('z', (void *)1) != -1 || errno != EBADF) return 14;
  errno = 0;
  if (ungetc(-1, (void *)0) != -1 || errno != EINVAL) return 22;
  errno = 0;
  if (ungetc(0x141, (void *)1) != -1 || errno != EBADF) return 23;
  errno = 0;
  if (ungetc(0x141, (void *)0) != 'A' || errno != 0) return 24;
  if (fgetc((void *)0) != 'A') return 25;
  if (ungetc(-2, (void *)0) != 254) return 26;
  if (fgetc((void *)0) != 254) return 27;
  errno = 0;
  if (feof((void *)3) != 0 || errno != EBADF) return 15;
  errno = 0;
  if (ferror((void *)3) != 1 || errno != EBADF) return 16;
  errno = 0;
  clearerr((void *)3);
  if (errno != EBADF) return 17;
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
  errno = 0;
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
if (jsStdinResult !== 42 || jsStdinStderr !== "stdin: no error\n") {
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
int *__error(void);
#define errno (*__error())
#define EBADF 9
int main(void) {
  int line[4];
  int text[] = {0x3042, '!', 0};
  int bad_text[] = {0x110000, 0};
  int partial_bad_text[] = {'P', 0x110000, 0};
  if (fwide((void *)0, 1) != 1) return 1;
  if (fwide((void *)0, -1) != 1) return 2;
  if (fwide((void *)0, 0) != 1) return 3;
  errno = 0;
  if (fwide((void *)3, 1) != 0 || errno != EBADF) return 21;
  if (fputwc('A', (void *)1) != 'A') return 4;
  if (putwc(0x3042, (void *)1) != 0x3042) return 5;
  if (fputws(text, (void *)2) != 2) return 6;
  if (putwchar('Q') != 'Q') return 7;
  errno = 0;
  if (fputwc('Z', (void *)0) != -1 || errno != EBADF) return 19;
  errno = 0;
  if (fputwc(0x110000, (void *)0) != -1 || errno != 0) return 23;
  errno = 0;
  if (fputwc(0x110000, (void *)1) != -1 || errno != 0) return 24;
  errno = 0;
  if (fputws(text, (void *)0) != -1 || errno != EBADF) return 20;
  errno = 0;
  if (fputws((void *)0, (void *)0) != -1 || errno != 0) return 27;
  errno = 0;
  if (fputws((void *)0, (void *)1) != -1 || errno != 0) return 28;
  errno = 0;
  if (fputws(bad_text, (void *)0) != -1 || errno != 0) return 25;
  errno = 0;
  if (fputws(bad_text, (void *)1) != -1 || errno != 0) return 26;
  errno = 0;
  if (fputws(partial_bad_text, (void *)1) != -1 || errno != 0) return 31;
  errno = 0;
  if (fgetwc((void *)1) != -1 || errno != EBADF) return 16;
  errno = 0;
  if (fgetws(line, 4, (void *)1) != 0 || errno != EBADF) return 17;
  errno = 0;
  if (fgetws((void *)0, 4, (void *)1) != 0 || errno != 0) return 29;
  errno = 0;
  if (fgetws((void *)0, 4, (void *)0) != 0 || errno != 0) return 30;
  errno = 0;
  if (ungetwc('Z', (void *)1) != -1 || errno != EBADF) return 18;
  errno = 0;
  if (ungetwc(0x3042, (void *)1) != -1 || errno != 0) return 22;
  if (fgetwc((void *)0) != 'x') return 8;
  if (ungetwc('Y', (void *)0) != 'Y') return 9;
  if (getwc((void *)0) != 'Y') return 10;
  if (getwchar() != 0x3042) return 11;
  if (fgetws(line, 4, (void *)0) != line) return 12;
  if (line[0] != '\\n' || line[1] != 0) return 13;
  errno = 0;
  if (ungetwc(0x3042, (void *)0) != -1 || errno != 0) return 14;
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
if (jsWideStdioResult !== 42 || jsWideStdout !== "AあQP" || jsWideStderr !== "あ!") {
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
  const diagnostic = err.diagnostics?.[0];
  if (!diagnostic || diagnostic.code !== "E2028" || diagnostic.severity !== "error" ||
      diagnostic.sourceId !== 1 || diagnostic.sourceName !== "input.c") {
    throw new Error(`toolchain error did not preserve structured source identity: ${JSON.stringify(err.diagnostics)}`);
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
