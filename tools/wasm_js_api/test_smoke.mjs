import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { createCompiler } from "./agc-wasm.js";

const wasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const wasm = await readFile(wasmPath);
const stderrChunks = [];
const terminations = [];
const compiler = await createCompiler(wasm, {
  onStderr(chunk) {
    stderrChunks.push(chunk);
  },
  onTerminate(event) {
    terminations.push(event);
  },
});
if (!compiler.limits.useHeapBuffers) {
  throw new Error("JS API did not enable wasm heap buffers");
}
const wat = compiler.compileWat("int main(){return 42;}\n");

if (!wat.includes("(func $main")) {
  throw new Error("compiled WAT does not contain main");
}
if (!wat.includes("(return (i32.const 42))")) {
  throw new Error("compiled WAT does not return 42");
}
const namedWat = compiler.compileWat({
  name: "main.c",
  source: "int main(void){return 43;}\n",
});
if (!namedWat.includes("(return (i32.const 43))")) {
  throw new Error("named source did not compile to WAT");
}

const largeSource = `/*${"x".repeat(40000)}*/\nint main(){return 7;}\n`;
const largeWat = compiler.compileWat(largeSource);
if (!largeWat.includes("(return (i32.const 7))")) {
  throw new Error("heap-buffer compile did not handle source larger than fixed buffer");
}

try {
  stderrChunks.length = 0;
  terminations.length = 0;
  compiler.compileWat("int main(void {\n");
  throw new Error("invalid source unexpectedly compiled");
} catch (err) {
  const message = String(err && err.message ? err.message : err);
  if (!message.includes("トークンが必要") || message.includes("timed out")) {
    throw new Error(`invalid source did not surface compiler diagnostics: ${message}`);
  }
  if (!stderrChunks.join("").includes("トークンが必要")) {
    throw new Error("invalid source did not stream diagnostics through onStderr");
  }
  if (terminations.length !== 1 || terminations[0].kind !== "exit" || terminations[0].status !== 1) {
    throw new Error(`invalid source did not report exit(1): ${JSON.stringify(terminations)}`);
  }
  if (!Array.isArray(err.diagnostics) || err.diagnostics.length === 0) {
    throw new Error("invalid source did not expose structured diagnostics");
  }
  const diagnostic = err.diagnostics[0];
  if (diagnostic.severity !== "error" || !/^E\d{4}$/.test(diagnostic.code) ||
      diagnostic.sourceId !== 0 || diagnostic.sourceName !== "input.c" ||
      diagnostic.start.line !== 1 || diagnostic.start.column <= 0) {
    throw new Error(`invalid structured syntax diagnostic: ${JSON.stringify(diagnostic)}`);
  }
}

try {
  compiler.compileObject({
    name: "player.c",
    source: "int f(void) { const int x = 1; x = 2; return x; }\n",
  });
  throw new Error("semantic error source unexpectedly compiled");
} catch (err) {
  const diagnostic = err.diagnostics?.[0];
  if (!diagnostic || diagnostic.severity !== "error" || diagnostic.code !== "E3077" ||
      diagnostic.sourceName !== "player.c" || !diagnostic.message.includes("const")) {
    throw new Error(`semantic error was not structured: ${JSON.stringify(err.diagnostics)}`);
  }
}

function expectCompileErrorDiagnostics(name, source) {
  try {
    compiler.compileObject({ name, source });
  } catch (err) {
    if (Array.isArray(err.diagnostics)) return err.diagnostics;
    throw err;
  }
  throw new Error(`${name} unexpectedly compiled`);
}

const implicitIntDiagnostics = expectCompileErrorDiagnostics(
  "implicit-int.c",
  "aaa;\nbb;\n",
);
if (implicitIntDiagnostics.length !== 2 ||
    implicitIntDiagnostics.some((diagnostic) =>
      diagnostic.code !== "E3088" || diagnostic.sourceName !== "implicit-int.c") ||
    implicitIntDiagnostics[0].start.line !== 1 || implicitIntDiagnostics[0].start.column !== 1 ||
    implicitIntDiagnostics[1].start.line !== 2 || implicitIntDiagnostics[1].start.column !== 1) {
  throw new Error(`implicit int diagnostics did not recover: ${JSON.stringify(implicitIntDiagnostics)}`);
}

const typedefResult = compiler.compileObjectWithDiagnostics({
  name: "typedef-name.c",
  source: "typedef int value_type;\nvalue_type value;\n",
});
if (!(typedefResult.object instanceof Uint8Array) || typedefResult.diagnostics.length !== 0) {
  throw new Error(`typedef name declaration regressed: ${JSON.stringify(typedefResult.diagnostics)}`);
}

for (const [name, source, expectedLine, expectedColumn] of [
  ["implicit-return.c", "implicit_return(void) { return 0; }\n", 1, 1],
  ["old-style-parameter.c", "int old_style(value) { return value; }\n", 1, 15],
  ["block-implicit-int.c", "int block_scope(void) { static local; return 0; }\n", 1, 32],
]) {
  const diagnostics = expectCompileErrorDiagnostics(name, source);
  if (diagnostics.length !== 1 || diagnostics[0].code !== "E3088" ||
      diagnostics[0].start.line !== expectedLine ||
      diagnostics[0].start.column !== expectedColumn) {
    throw new Error(`${name} C11 mode behavior regressed: ${JSON.stringify(diagnostics)}`);
  }
}

const recoveredFunctionDiagnostics = expectCompileErrorDiagnostics(
  "multiple-functions.c",
  "int first(void) {\n  int a = ;\n  return a;\n}\n\n" +
    "int second(void) {\n  int b = ;\n  return b;\n}\n",
);
if (recoveredFunctionDiagnostics.length !== 2 ||
    recoveredFunctionDiagnostics.some((diagnostic) => diagnostic.code !== "E3045") ||
    recoveredFunctionDiagnostics[0].start.line !== 2 ||
    recoveredFunctionDiagnostics[1].start.line !== 7) {
  throw new Error(
    `independent function diagnostics did not recover: ${JSON.stringify(recoveredFunctionDiagnostics)}`,
  );
}

const opaqueSourceName = `https://example.invalid/../${"long-name-".repeat(32)}warning.c`;
const warningResult = compiler.compileObjectWithDiagnostics({
  name: opaqueSourceName,
  source: "int f(void) { int x = 1.5; return x; }\n",
});
if (!(warningResult.object instanceof Uint8Array)) {
  throw new Error("compileObjectWithDiagnostics did not return object bytes");
}
const warning = warningResult.diagnostics.find((diagnostic) => diagnostic.severity === "warning");
if (!warning || warning.code !== "W3010" || warning.sourceName !== opaqueSourceName) {
  throw new Error(`successful compile warning was not structured: ${JSON.stringify(warningResult.diagnostics)}`);
}
if (!Object.isFrozen(warningResult.diagnostics) || !Object.isFrozen(warning) ||
    !Object.isFrozen(warning.start) || !Object.isFrozen(warning.end) ||
    !Object.isFrozen(warning.notes)) {
  throw new Error("successful compile diagnostics are not an immutable snapshot");
}
const warningSnapshot = JSON.stringify(warningResult.diagnostics);
const isolatedAdapter = Number(compiler.instance.exports.agc_wasm_adapter_create());
if (!isolatedAdapter ||
    Number(compiler.instance.exports.agc_wasm_adapter_diagnostic_count(
      isolatedAdapter,
    )) !== 0 ||
    Number(compiler.instance.exports.agc_wasm_adapter_set_diagnostic_locale(
      isolatedAdapter, 1,
    )) !== 0 ||
    Number(compiler.instance.exports.agc_wasm_adapter_diagnostic_set_limits(
      isolatedAdapter, 1, 256,
    )) !== 0) {
  throw new Error("independent Wasm adapter state could not be configured");
}
compiler.compileObjectWithDiagnostics({
  name: "later.c",
  source: "int later(void) { int y = 2.5; return y; }\n",
});
if (Number(compiler.instance.exports.agc_wasm_adapter_diagnostic_count(
      isolatedAdapter,
    )) !== 0 ||
    Number(compiler.instance.exports.agc_wasm_adapter_destroy(
      isolatedAdapter,
    )) !== 0) {
  throw new Error("a compile leaked diagnostics into another Wasm adapter");
}
if (JSON.stringify(warningResult.diagnostics) !== warningSnapshot) {
  throw new Error("a later compile changed an earlier warning snapshot");
}
if (!stderrChunks.join("").includes("W3010") || !stderrChunks.join("").includes(opaqueSourceName)) {
  throw new Error("structured warning was not also emitted through onStderr");
}

for (const [name, source, expectedCodes] of [
  ["self-assignment", "int f(void){int x;x=x;return 0;}", ["W3012"]],
  ["self-comparison", "int f(void){int x=1;return x==x;}", ["W3013"]],
  ["identical-logical", "int f(void){int x=1;return x&&x;}", ["W3020"]],
  ["sign-compare", "int f(void){unsigned u=1;int s=-1;return s<u;}", ["W3018"]],
  ["unsigned-zero", "int f(void){unsigned u=1;return u<0;}", ["W3019"]],
  ["pointer-integer", "int f(void){int *p;return p==5;}", ["W3022"]],
  ["integer-overflow", "int f(void){return 2147483647+1;}", ["W3023"]],
  ["shift-range", "int f(void){return 1<<32;}", ["W3014"]],
  ["divide-zero", "int f(void){return 1/0;}", ["W3015"]],
  ["condition", "int f(void){int x=0;if(x=1){}while(x,0){}return x;}", ["W3007", "W3008"]],
  ["stack-address", "int *f(void){int x=0;return &x;}", ["W3006"]],
  ["constant-overflow", "int f(void){char c=200;return c;}", ["W3011"]],
]) {
  const result = compiler.compileObjectWithDiagnostics({
    name: `typed-hir-${name}.c`,
    source: `${source}\n`,
  });
  const actualCodes = result.diagnostics.map(({ code }) => code);
  if (actualCodes.length !== expectedCodes.length ||
      actualCodes.some((code, index) => code !== expectedCodes[index])) {
    throw new Error(
      `${name} Typed HIR warnings regressed: ${JSON.stringify(result.diagnostics)}`,
    );
  }
}

const localizedWarningSource = {
  name: "localized-warning.c",
  source: 'int main(void) { return printf("Hello\\n"); }\n',
};
function compileLocalizedWarning(locale) {
  const result = compiler.compileObjectWithDiagnostics(
    localizedWarningSource, { diagnosticLocale: locale },
  );
  const diagnostic = result.diagnostics.find(({ code }) => code === "W3016");
  if (!diagnostic) {
    throw new Error(
      `localized W3016 was not emitted for ${locale}: ${JSON.stringify(result.diagnostics)}`,
    );
  }
  return diagnostic;
}
const englishImplicitFunction = compileLocalizedWarning("en");
const japaneseImplicitFunction = compileLocalizedWarning("ja");
const englishImplicitFunctionAgain = compileLocalizedWarning("en");
if (!englishImplicitFunction.message.includes("function 'printf' is not declared") ||
    englishImplicitFunction.message.includes("関数") ||
    !japaneseImplicitFunction.message.includes("関数 'printf' は宣言されていません") ||
    japaneseImplicitFunction.message.includes("function is not declared") ||
    !englishImplicitFunctionAgain.message.includes("function 'printf' is not declared")) {
  throw new Error(
    `diagnostic locale leaked across compiles: ${JSON.stringify({
      englishImplicitFunction,
      japaneseImplicitFunction,
      englishImplicitFunctionAgain,
    })}`,
  );
}

function compileLocalizedExpectedToken(locale) {
  stderrChunks.length = 0;
  try {
    compiler.compileWat({
      name: "localized-error.c",
      source: "int main(void {\n",
    }, { diagnosticLocale: locale });
  } catch (err) {
    const diagnostic = err.diagnostics?.find(({ code }) => code === "E2006");
    if (!diagnostic) {
      throw new Error(
        `localized E2006 was not emitted for ${locale}: ${JSON.stringify(err.diagnostics)}`,
      );
    }
    return {
      diagnostic,
      rendered: String(err && err.message ? err.message : err),
      streamed: stderrChunks.join(""),
    };
  }
  throw new Error(`localized E2006 source unexpectedly compiled for ${locale}`);
}
const englishExpectedToken = compileLocalizedExpectedToken("en");
const japaneseExpectedToken = compileLocalizedExpectedToken("ja");
const englishExpectedTokenAgain = compileLocalizedExpectedToken("en");
for (const result of [englishExpectedToken, englishExpectedTokenAgain]) {
  if (!result.diagnostic.message.includes("Expected token is missing") ||
      !result.rendered.includes("actual token") ||
      !result.streamed.includes("actual token") ||
      result.rendered.includes("実際のトークン") ||
      result.streamed.includes("実際のトークン")) {
    throw new Error(
      `English E2006 mixed diagnostic locales: ${JSON.stringify(result)}`,
    );
  }
}
if (!japaneseExpectedToken.diagnostic.message.includes("トークンが必要です") ||
    !japaneseExpectedToken.rendered.includes("実際のトークン") ||
    !japaneseExpectedToken.streamed.includes("実際のトークン") ||
    japaneseExpectedToken.rendered.includes("actual token") ||
    japaneseExpectedToken.streamed.includes("actual token")) {
  throw new Error(
    `Japanese E2006 mixed diagnostic locales: ${JSON.stringify(japaneseExpectedToken)}`,
  );
}

for (const diagnostic of [
  japaneseImplicitFunction,
  englishImplicitFunctionAgain,
]) {
  if (diagnostic.code !== englishImplicitFunction.code ||
      diagnostic.severity !== englishImplicitFunction.severity ||
      diagnostic.sourceName !== englishImplicitFunction.sourceName ||
      JSON.stringify(diagnostic.start) !== JSON.stringify(englishImplicitFunction.start) ||
      JSON.stringify(diagnostic.end) !== JSON.stringify(englishImplicitFunction.end)) {
    throw new Error("diagnostic locale changed code, severity, source, or coordinates");
  }
}
const declaredPrintf = compiler.compileObjectWithDiagnostics({
  name: "declared-printf.c",
  source: '#include <stdio.h>\nint main(void) { return printf("Hello\\n"); }\n',
}, {
  diagnosticLocale: "en",
  headers: {
    "stdio.h": "int printf(const char *, ...);\n",
  },
});
if (declaredPrintf.diagnostics.some(({ code }) => code === "W3016")) {
  throw new Error(
    `declared printf unexpectedly emitted W3016: ${JSON.stringify(declaredPrintf.diagnostics)}`,
  );
}
try {
  compiler.compileObject(localizedWarningSource, { diagnosticLocale: "fr" });
  throw new Error("unsupported diagnostic locale unexpectedly compiled");
} catch (err) {
  if (err.message === "unsupported diagnostic locale unexpectedly compiled") throw err;
  if (!(err instanceof RangeError) || !err.message.includes("diagnosticLocale")) throw err;
}

function expectContinuationDiagnostic(source, expectedCode) {
  try {
    compiler.compileObject(source, {
      continuation: { entry: "main", frameCondition: "frame_gate" },
      diagnosticLocale: "en",
    });
  } catch (err) {
    const diagnostic = err.diagnostics?.find(({ code }) => code === expectedCode);
    if (diagnostic) return diagnostic;
    throw new Error(
      `expected ${expectedCode}, got ${JSON.stringify(err.diagnostics)}`,
    );
  }
  throw new Error(`${expectedCode} continuation source unexpectedly compiled`);
}
for (const [expectedCode, source] of [
  ["E3089", "int frame_gate(void); void main(void) {}\n"],
  ["E3090", "void frame_gate(void); int main(void) { while (frame_gate()) {} return 0; }\n"],
  ["E3091", "int frame_gate(void); int main(void) { while (frame_gate()) { goto done; } done: return 0; }\n"],
  ["E3092", "int frame_gate(void); int main(void) { int n = 2; int a[n]; while (frame_gate()) { a[0] = 1; } return a[0]; }\n"],
  ["E3093", "int frame_gate(void); void *alloca(unsigned long); int main(void) { while (frame_gate()) { alloca(4); } return 0; }\n"],
  ["E3094", "int frame_gate(void); int main(void) { int enabled = 1; while (frame_gate() && enabled) enabled = 0; return 0; }\n"],
  ["E3095", "int frame_gate(void); int main(void) { while (frame_gate()) { frame_gate(); } return 0; }\n"],
]) {
  expectContinuationDiagnostic(source, expectedCode);
}

let errorDiagnostics;
try {
  compiler.compileObject({
    name: "snapshot-error.c",
    source: "int broken(void) { const int x = 1; x = 2; return x; }\n",
  });
  throw new Error("snapshot error source unexpectedly compiled");
} catch (err) {
  if (err.message === "snapshot error source unexpectedly compiled") throw err;
  errorDiagnostics = err.diagnostics;
  if (!Array.isArray(errorDiagnostics) || errorDiagnostics[0]?.code !== "E3077" ||
      !Object.isFrozen(errorDiagnostics) || !Object.isFrozen(errorDiagnostics[0])) {
    throw new Error(`compile error diagnostics are not an immutable snapshot: ${JSON.stringify(errorDiagnostics)}`);
  }
}
const errorSnapshot = JSON.stringify(errorDiagnostics);
compiler.compileObject("int after_error(void) { return 0; }\n");
if (JSON.stringify(errorDiagnostics) !== errorSnapshot) {
  throw new Error("a later compile changed an earlier error snapshot");
}

try {
  compiler.compileObject({ name: "bad\0name.c", source: "int f(void) { return 0; }\n" });
  throw new Error("NUL source name unexpectedly compiled");
} catch (err) {
  if (!(err instanceof TypeError) || !err.message.includes("NUL")) throw err;
}

const unicodeSource = "int f(void) { /* あ */ int x = ; return x; }\n";
try {
  compiler.compileObject(unicodeSource);
  throw new Error("Unicode position source unexpectedly compiled");
} catch (err) {
  const diagnostic = err.diagnostics?.[0];
  const semicolonIndex = unicodeSource.indexOf(";");
  const expectedOffset = new TextEncoder().encode(unicodeSource.slice(0, semicolonIndex)).length;
  if (!diagnostic || diagnostic.start.offset !== expectedOffset ||
      diagnostic.start.column !== expectedOffset + 1 ||
      diagnostic.end.offset !== expectedOffset + 1 ||
      diagnostic.end.column !== expectedOffset + 2) {
    throw new Error(
      `Unicode diagnostic position is not UTF-8 byte based: ${JSON.stringify(diagnostic)}`,
    );
  }
}

const macroSource = "#define BAD ;\nint f(void) { int x = BAD return x; }\n";
try {
  compiler.compileObject(macroSource);
  throw new Error("macro diagnostic source unexpectedly compiled");
} catch (err) {
  const diagnostic = err.diagnostics?.[0];
  const invocationIndex = macroSource.indexOf("BAD return");
  if (!diagnostic || diagnostic.start.line !== 2 ||
      diagnostic.start.offset !== invocationIndex ||
      diagnostic.end.offset !== invocationIndex + "BAD".length) {
    throw new Error(`macro diagnostic did not retain invocation range: ${JSON.stringify(diagnostic)}`);
  }
}

if (compiler.diagnosticCoordinateSystem.encoding !== "utf-8" ||
    compiler.diagnosticCoordinateSystem.end !== "exclusive") {
  throw new Error("diagnostic coordinate system is not documented by the JS API");
}

const fixedCompiler = await createCompiler(wasm, { useHeapBuffers: false });
const fixedNamedResult = fixedCompiler.compileObjectWithDiagnostics({
  name: "fixed.c",
  source: "int f(void) { int x = 1.5; return x; }\n",
});
if (fixedNamedResult.diagnostics[0]?.sourceName !== "fixed.c") {
  throw new Error(`fixed-buffer named source failed: ${JSON.stringify(fixedNamedResult.diagnostics)}`);
}
const fixedVirtualObject = fixedCompiler.compileObject({
  name: "fixed-main.c",
  source: '#include "fixed.h"\nint f(void) { return FIXED_VALUE; }\n',
}, { headers: { "fixed.h": "#define FIXED_VALUE 7\n" } });
if (!(fixedVirtualObject instanceof Uint8Array) || fixedVirtualObject.length === 0) {
  throw new Error("fixed-buffer virtual header source did not compile");
}

function expectVirtualDiagnostic(source, options, code, sourceName) {
  try {
    compiler.compileObject({ name: "main.c", source }, options);
    throw new Error(`${code} virtual header case unexpectedly compiled`);
  } catch (err) {
    const diagnostic = err.diagnostics?.[0];
    if (diagnostic?.code !== code ||
        (sourceName !== undefined && diagnostic.sourceName !== sourceName)) {
      throw new Error(`expected ${code}, got ${JSON.stringify(err.diagnostics)}`);
    }
    return diagnostic;
  }
}

const headerDiagnostic = expectVirtualDiagnostic(
  "#include \"player.h\"\nint f(void) { return 0; }\n",
  { headers: { "player.h": "int ok;\nint broken = ;\n" } },
  "E3045",
  "player.h",
);
if (headerDiagnostic.start.line !== 2 || headerDiagnostic.start.column !== 14) {
  throw new Error(`virtual header position was not preserved: ${JSON.stringify(headerDiagnostic)}`);
}

expectVirtualDiagnostic('#include "missing.h"\n', { headers: {} }, "E1034");
expectVirtualDiagnostic('#include "README.md"\n', { headers: {} }, "E1034");
expectVirtualDiagnostic('#include "a.h"\n', {
  headers: { "a.h": '#include "b.h"\n', "b.h": '#include "a.h"\n' },
}, "E1005");
expectVirtualDiagnostic("int f(void){return 0;}\n", {
  headers: { "dir/../invalid.h": "" },
}, "E1003");

for (const [pathName, code] of [
  ["/absolute.h", "E1002"],
  ["https://example.invalid/x.h", "E1002"],
  ["dir//empty.h", "E1002"],
  ["dir/./dot.h", "E1002"],
  ["../parent.h", "E1003"],
  ["dir\\backslash.h", "E1002"],
]) {
  expectVirtualDiagnostic(`#include "${pathName}"\n`, { headers: {} }, code);
}

compiler.compileObject({ name: "main.c", source: '#include "a.h"\nint f(void){return 0;}\n' }, {
  headers: { "a.h": "/**/" },
  headerLimits: { maxFiles: 1, maxFileBytes: 4, maxTotalBytes: 4, maxIncludeDepth: 1 },
});
expectVirtualDiagnostic("int f(void){return 0;}\n", {
  headers: { "a.h": "", "b.h": "" },
  headerLimits: { maxFiles: 1 },
}, "E1039");
expectVirtualDiagnostic("int f(void){return 0;}\n", {
  headers: { "a.h": "/**/" },
  headerLimits: { maxFileBytes: 3 },
}, "E1040");
expectVirtualDiagnostic("int f(void){return 0;}\n", {
  headers: { "a.h": "/**/", "b.h": "/**/" },
  headerLimits: { maxTotalBytes: 7 },
}, "E1041");

compiler.compileObject({ name: "main.c", source: '#include "dir/a.h"\nint f(void){return VALUE;}\n' }, {
  headers: { "dir/a.h": '#include "b.h"\n', "dir/b.h": "#define VALUE 1\n" },
  headerLimits: { maxIncludeDepth: 2 },
});
compiler.compileObject({
  name: "src/main.c",
  source: '#include "player.h"\nint f(void){return PLAYER_VALUE;}\n',
}, {
  headers: { "src/player.h": "#define PLAYER_VALUE 2\n" },
});
expectVirtualDiagnostic('#include "a.h"\n', {
  headers: {
    "a.h": '#include "b.h"\n',
    "b.h": '#include "c.h"\n',
    "c.h": "#define VALUE 1\n",
  },
  headerLimits: { maxIncludeDepth: 2 },
}, "E1004");

const objectBytes = compiler.compileObject("int other(void); int main(void){return other();}\n");
if (objectBytes[0] !== 0x00 || objectBytes[1] !== 0x61 ||
    objectBytes[2] !== 0x73 || objectBytes[3] !== 0x6d) {
  throw new Error("compileObject output is not a wasm object module");
}
const outDir = "build/wasm_selfhost_api_smoke";
await mkdir(outDir, { recursive: true });
const objPath = path.join(outDir, "compile_object_from_api.o");
await writeFile(objPath, objectBytes);
try {
  const out = execFileSync("wasm-objdump", ["-x", objPath], { encoding: "utf8" });
  if (!out.includes("linking") || !out.includes("reloc.CODE")) {
    throw new Error("compileObject output is missing Wasm object metadata");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

fixedCompiler.dispose();
compiler.dispose();
compiler.dispose();
try {
  compiler.compileWat("int disposed(void) { return 0; }\n");
  throw new Error("disposed compiler unexpectedly accepted compilation");
} catch (err) {
  if (!String(err?.message).includes("disposed")) throw err;
}

console.log("ag_c wasm JS API smoke: ok");
