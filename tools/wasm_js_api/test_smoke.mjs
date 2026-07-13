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
  compiler.compileWat("int main( {\n");
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
compiler.compileObjectWithDiagnostics({
  name: "later.c",
  source: "int later(void) { int y = 2.5; return y; }\n",
});
if (JSON.stringify(warningResult.diagnostics) !== warningSnapshot) {
  throw new Error("a later compile changed an earlier warning snapshot");
}
if (!stderrChunks.join("").includes("W3010") || !stderrChunks.join("").includes(opaqueSourceName)) {
  throw new Error("structured warning was not also emitted through onStderr");
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
  "E3064",
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

console.log("ag_c wasm JS API smoke: ok");
