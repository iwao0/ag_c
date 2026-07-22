import { readFile } from "node:fs/promises";
import { execFileSync } from "node:child_process";
import assert from "node:assert/strict";
import {
  AgcResourceLimitError,
  createCompiler,
} from "./agc-wasm.js";

const wasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const nativeAnalysisPath = process.argv[3] || "build/test_language_analysis";
const compiler = await createCompiler(await readFile(wasmPath));

const paritySource = {
  name: "main.c",
  source: "/* 日本語 */\n#include <parity.h>\ntypedef unsigned long Size; int global_value;\nint main(int parameter) { const int *local; parity_",
};
const parityHeaders = {
  "parity.h": "#define PARITY_WIDTH 320\nint parity_sum(int left, int right);\n",
};
const wasmParity = compiler.analyzeSource(paritySource, {
  headers: parityHeaders,
  cursor: {
    sourceName: paritySource.name,
    byteOffset: Buffer.byteLength(paritySource.source),
  },
});
const nativeParity = JSON.parse(execFileSync(
  nativeAnalysisPath, ["--parity-json"], { encoding: "utf8" },
));
assert.deepStrictEqual(wasmParity, nativeParity,
  "native and Wasm language-analysis snapshots differ");

const source = {
  name: "main.c",
  source: "#include <game.h>\nint main(int color) { int local; screen_",
};
const result = compiler.analyzeSource(source, {
  cursor: { sourceName: source.name, byteOffset: Buffer.byteLength(source.source) },
  headers: {
    "game.h": "#define GAME_SCREEN_WIDTH 320\nvoid screen_clear(int color);\n",
  },
});

function symbol(snapshot, name, kind) {
  return snapshot.completionItems.find((item) =>
    item.name === name && item.kind === kind);
}

if (!symbol(result, "GAME_SCREEN_WIDTH", "macro") ||
    !symbol(result, "screen_clear", "function") ||
    !symbol(result, "color", "parameter") ||
    !symbol(result, "local", "object")) {
  throw new Error(`analysis snapshot omitted visible symbols: ${JSON.stringify(result)}`);
}
if (!result.partial || result.diagnostics.length === 0 ||
    !result.diagnostics.some((diagnostic) =>
      diagnostic.code === "AGC_PARTIAL_IDENTIFIER")) {
  throw new Error(`incomplete source did not return a partial diagnostic: ${JSON.stringify(result)}`);
}
if (!Object.isFrozen(result) || !Object.isFrozen(result.completionItems) ||
    !Object.isFrozen(result.completionItems[0]) ||
    !Object.isFrozen(result.completionItems[0].declaration.start)) {
  throw new Error("analysis result is not a deeply immutable snapshot");
}

const functionSource = {
  name: "function.c",
  source: "int format(const char *value, ...); int main(void) { format",
};
const functionResult = compiler.analyzeSource(functionSource, {
  cursor: {
    sourceName: functionSource.name,
    byteOffset: Buffer.byteLength(functionSource.source),
  },
});
if (functionResult.hover?.name !== "format" ||
    functionResult.hover.function?.returnType !== "int" ||
    functionResult.hover.function?.parameters.length !== 1 ||
    functionResult.hover.function?.variadic !== true) {
  throw new Error(`function hover is not structured: ${JSON.stringify(functionResult.hover)}`);
}
if (functionResult.hover.function.parameters[0].name !== "value") {
  throw new Error(`function parameter name was lost: ${JSON.stringify(functionResult.hover)}`);
}

const virtualHoverSource = {
  name: "virtual-hover.c",
  source: "#include <symbols.h>\n" +
    "int main(void) { return header_function(header_object) + " +
    "HEADER_LIMIT + (int)sizeof(HeaderSize); }\n",
};
const virtualHoverHeaders = {
  "symbols.h": "#define HEADER_LIMIT 7\n" +
    "typedef unsigned long HeaderSize;\n" +
    "extern int header_object;\n" +
    "int header_function(int value);\n",
};
const functionStart = virtualHoverSource.source.indexOf("header_function");
for (const byteOffset of [
  functionStart,
  functionStart + 7,
  functionStart + "header_function".length,
]) {
  const hoverResult = compiler.analyzeSource(virtualHoverSource, {
    headers: virtualHoverHeaders,
    cursor: { sourceName: virtualHoverSource.name, byteOffset },
  });
  if (hoverResult.hover?.name !== "header_function" ||
      hoverResult.hover.kind !== "function" ||
      hoverResult.hover.signature !== "int (int)" ||
      hoverResult.hover.declaration.sourceName !== "symbols.h") {
    throw new Error(`virtual header function hover failed: ${JSON.stringify(hoverResult)}`);
  }
}
for (const [name, kind] of [
  ["header_object", "object"],
  ["HeaderSize", "typedef"],
  ["HEADER_LIMIT", "macro"],
]) {
  const start = virtualHoverSource.source.indexOf(name);
  const hoverResult = compiler.analyzeSource(virtualHoverSource, {
    headers: virtualHoverHeaders,
    cursor: {
      sourceName: virtualHoverSource.name,
      byteOffset: start + Buffer.byteLength(name),
    },
  });
  if (hoverResult.hover?.name !== name || hoverResult.hover.kind !== kind ||
      hoverResult.hover.declaration.sourceName !== "symbols.h") {
    throw new Error(`virtual header ${kind} hover failed: ${JSON.stringify(hoverResult)}`);
  }
}

const memberSource = {
  name: "member.c",
  source: "struct Player { int score; }; int main(void) { struct Player p; p.sc",
};
const memberResult = compiler.analyzeSource(memberSource, {
  cursor: {
    sourceName: memberSource.name,
    byteOffset: Buffer.byteLength(memberSource.source),
  },
});
if (!symbol(memberResult, "score", "member")) {
  throw new Error(`member completion is missing: ${JSON.stringify(memberResult)}`);
}

const macroSource = {
  name: "macros.c",
  source: "#define REMOVED 1\n#undef REMOVED\n#if 0\n#define DISABLED 2\n#else\n#define ENABLED 3\n#endif\nint main(void) { EN",
};
const macroResult = compiler.analyzeSource(macroSource, {
  cursor: {
    sourceName: macroSource.name,
    byteOffset: Buffer.byteLength(macroSource.source),
  },
});
if (symbol(macroResult, "REMOVED", "macro") ||
    symbol(macroResult, "DISABLED", "macro") ||
    !symbol(macroResult, "ENABLED", "macro")) {
  throw new Error(`analysis returned the wrong active macros: ${JSON.stringify(macroResult)}`);
}

const lateErrorSource = {
  name: "late-error.c",
  source: "int before_error; int main(void) { bef\nthis is invalid syntax after the cursor",
};
const lateErrorCursor = Buffer.byteLength(lateErrorSource.source.slice(
  0, lateErrorSource.source.indexOf("bef\n") + 3,
));
const lateErrorResult = compiler.analyzeSource(lateErrorSource, {
  cursor: { sourceName: lateErrorSource.name, byteOffset: lateErrorCursor },
});
if (!lateErrorResult.partial || !symbol(lateErrorResult, "before_error", "object")) {
  throw new Error(`later syntax error discarded an earlier symbol: ${JSON.stringify(lateErrorResult)}`);
}

const semanticErrorSource = {
  name: "semantic-error.c",
  source: "int before_error; int main(void) { int local; missing_name = 1; loc",
};
const semanticErrorResult = compiler.analyzeSource(semanticErrorSource, {
  cursor: {
    sourceName: semanticErrorSource.name,
    byteOffset: Buffer.byteLength(semanticErrorSource.source),
  },
});
if (!semanticErrorResult.partial || semanticErrorResult.diagnostics.length === 0 ||
    !semanticErrorResult.diagnostics.some((diagnostic) =>
      diagnostic.code === "AGC_PARTIAL_SEMANTIC") ||
    !symbol(semanticErrorResult, "before_error", "object") ||
    !symbol(semanticErrorResult, "local", "object")) {
  throw new Error(`semantic recovery lost partial symbols: ${JSON.stringify(semanticErrorResult)}`);
}

const missingHeaderSource = {
  name: "missing-header.c",
  source: "#include <not-registered.h>\nint after_missing_header;",
};
const missingHeaderResult = compiler.analyzeSource(missingHeaderSource, {
  cursor: {
    sourceName: missingHeaderSource.name,
    byteOffset: Buffer.byteLength(missingHeaderSource.source),
  },
});
if (!missingHeaderResult.partial || missingHeaderResult.diagnostics.length === 0) {
  throw new Error(`missing virtual header was not captured safely: ${JSON.stringify(missingHeaderResult)}`);
}

const utf8Source = {
  name: "utf8.c",
  source: "/* 日本語 */ int player; int main(void) { pla",
};
const utf8Result = compiler.analyzeSource(utf8Source, {
  cursor: {
    sourceName: utf8Source.name,
    byteOffset: Buffer.byteLength(utf8Source.source),
  },
});
const utf8Player = symbol(utf8Result, "player", "object");
if (!utf8Player || utf8Player.declaration.start.offset !==
    Buffer.byteLength(utf8Source.source.slice(0, utf8Source.source.indexOf("player")))) {
  throw new Error(`UTF-8 declaration range is wrong: ${JSON.stringify(utf8Player)}`);
}

const oldNames = result.completionItems.map((item) => item.name).join("\0");
compiler.analyzeSource(
  { name: "other.c", source: "int other;" },
  { cursor: { sourceName: "other.c", byteOffset: 10 } },
);
if (oldNames !== result.completionItems.map((item) => item.name).join("\0")) {
  throw new Error("a later analysis mutated an earlier snapshot");
}

const alternatingA = {
  name: "alternating-a.c",
  source: "#define ONLY_A 7\nint global_a; int main(void) { " +
    "int local_a; missing_a = ONLY_A; loc",
};
const alternatingB = {
  name: "alternating-b.c",
  source: "#define ONLY_B 9\nint global_b; int main(void) { int local_b; loc",
};
const analyzeAtEnd = (input, options = {}) => compiler.analyzeSource(input, {
  ...options,
  cursor: {
    sourceName: input.name,
    byteOffset: Buffer.byteLength(input.source),
  },
});
const alternatingAResult = analyzeAtEnd(alternatingA);
const alternatingBResult = analyzeAtEnd(alternatingB);
const alternatingAAgain = analyzeAtEnd(alternatingA);
if (!symbol(alternatingAResult, "ONLY_A", "macro") ||
    !symbol(alternatingAResult, "global_a", "object") ||
    !symbol(alternatingAResult, "local_a", "object") ||
    !alternatingAResult.diagnostics.some((diagnostic) =>
      diagnostic.code === "AGC_PARTIAL_SEMANTIC") ||
    symbol(alternatingBResult, "ONLY_A", "macro") ||
    symbol(alternatingBResult, "global_a", "object") ||
    symbol(alternatingBResult, "local_a", "object") ||
    !symbol(alternatingBResult, "ONLY_B", "macro") ||
    !symbol(alternatingBResult, "global_b", "object") ||
    !symbol(alternatingBResult, "local_b", "object") ||
    alternatingBResult.diagnostics.some((diagnostic) =>
      diagnostic.code === "AGC_PARTIAL_SEMANTIC") ||
    symbol(alternatingAAgain, "ONLY_B", "macro") ||
    symbol(alternatingAAgain, "global_b", "object") ||
    symbol(alternatingAAgain, "local_b", "object")) {
  throw new Error("analysis state leaked between alternating sources");
}

const changingHeaderSource = {
  name: "changing-header.c",
  source: "#include <changing.h>\nint main(void) { HEADER_",
};
const headerAResult = analyzeAtEnd(changingHeaderSource, {
  headers: {
    "changing.h": "#define HEADER_A 1\nint header_a(void);\n",
  },
});
const headerBResult = analyzeAtEnd(changingHeaderSource, {
  headers: {
    "changing.h": "#define HEADER_B 2\nint header_b(void);\n",
  },
});
if (!symbol(headerAResult, "HEADER_A", "macro") ||
    !symbol(headerAResult, "header_a", "function") ||
    symbol(headerAResult, "HEADER_B", "macro") ||
    !symbol(headerBResult, "HEADER_B", "macro") ||
    !symbol(headerBResult, "header_b", "function") ||
    symbol(headerBResult, "HEADER_A", "macro") ||
    symbol(headerBResult, "header_a", "function")) {
  throw new Error("virtual header state leaked between analyses");
}

const localeSource = {
  name: "locale-analysis.c",
  source: "#include <missing-locale.h>\nint value;",
};
const localizedResults = ["en", "ja", "en"].map((diagnosticLocale) =>
  analyzeAtEnd(localeSource, { diagnosticLocale }));
const localizedDiagnostics = localizedResults.map((snapshot) =>
  snapshot.diagnostics.find((diagnostic) => diagnostic.code === "E1034"));
if (localizedDiagnostics.some((diagnostic) => !diagnostic) ||
    /[\u3040-\u30ff\u3400-\u9fff]/u.test(localizedDiagnostics[0].message) ||
    !/[\u3040-\u30ff\u3400-\u9fff]/u.test(localizedDiagnostics[1].message) ||
    localizedDiagnostics[0].message !== localizedDiagnostics[2].message) {
  throw new Error(`analysis diagnostic locale leaked: ${JSON.stringify(localizedDiagnostics)}`);
}

try {
  compiler.analyzeSource(
    { name: "limit.c", source: "int first; int second;" },
    {
      cursor: { sourceName: "limit.c", byteOffset: 22 },
      limits: { maxAnalysisSymbols: 1 },
    },
  );
  throw new Error("analysis symbol limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS" ||
      error.limit !== "maxAnalysisSymbols" || error.actual <= error.max) {
    throw error;
  }
}

try {
  compiler.analyzeSource(
    { name: "source-limit.c", source: "int source_limit;" },
    {
      cursor: { sourceName: "source-limit.c", byteOffset: 17 },
      limits: { maxSourceBytes: 4 },
    },
  );
  throw new Error("analysis source byte limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_SOURCE_BYTES" ||
      error.limit !== "maxSourceBytes" || error.actual <= error.max) {
    throw error;
  }
}

for (const [badSource, options] of [
  [{ name: "bad.c", source: "int x;" },
   { cursor: { sourceName: "missing.c", byteOffset: 6 } }],
  [{ name: "bad.c", source: "int x;" },
   { cursor: { sourceName: "bad.c", byteOffset: 7 } }],
]) {
  try {
    compiler.analyzeSource(badSource, options);
    throw new Error("malformed analysis request unexpectedly succeeded");
  } catch (error) {
    if (!(error instanceof TypeError) && !(error instanceof RangeError)) throw error;
  }
}

const compiled = compiler.compileWat("int main(void) { return 21; }");
if (!compiled.includes("(return (i32.const 21))")) {
  throw new Error("language analysis changed the compile API");
}

const afterCompile = analyzeAtEnd({
  name: "after-compile.c",
  source: "int after_compile; int main(void) { after_",
});
if (!symbol(afterCompile, "after_compile", "object") ||
    symbol(afterCompile, "HEADER_B", "macro") ||
    afterCompile.diagnostics.some((diagnostic) =>
      diagnostic.code === "AGC_PARTIAL_SEMANTIC")) {
  throw new Error("language analysis did not recover after compile session replacement");
}

const rawExports = compiler.instance.exports;
const rawMemory = compiler.memory;
const rawMalloc = rawExports.malloc;
const rawFree = rawExports.free;
const rawAnalyzeExport = rawExports.agc_wasm_adapter_analyze_source_virtual;
const rawGenerationExport = rawExports.agc_wasm_adapter_session_generation;
const rawCreateExport = rawExports.agc_wasm_adapter_create;
const rawDestroyExport = rawExports.agc_wasm_adapter_destroy;
if (typeof rawMalloc !== "function" || typeof rawFree !== "function" ||
    typeof rawAnalyzeExport !== "function" ||
    typeof rawGenerationExport !== "function") {
  throw new Error("Wasm adapter session reuse instrumentation is unavailable");
}

const rawEncoder = new TextEncoder();
function rawAllocate(bytes) {
  const address = Number(rawMalloc(BigInt(bytes.length)));
  if (!address) throw new Error("raw adapter allocation failed");
  new Uint8Array(rawMemory.buffer).set(bytes, address);
  return address;
}

function rawHeaderBundle(path, source) {
  const pathBytes = rawEncoder.encode(path);
  const sourceBytes = rawEncoder.encode(source);
  const bytes = new Uint8Array(12 + pathBytes.length + sourceBytes.length + 2);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, 1, true);
  view.setUint32(4, pathBytes.length, true);
  view.setUint32(8, sourceBytes.length, true);
  bytes.set(pathBytes, 12);
  bytes.set(sourceBytes, 13 + pathBytes.length);
  return bytes;
}

function rawCString(address) {
  const bytes = new Uint8Array(rawMemory.buffer);
  let end = address;
  while (end < bytes.length && bytes[end] !== 0) end++;
  return new TextDecoder().decode(bytes.subarray(address, end));
}

function rawAnalyze(handle, input, maxSymbols = 4096, cursorOffset = null,
                    header = null) {
  const sourceBytes = rawEncoder.encode(`${input.source}\0`);
  const nameBytes = rawEncoder.encode(`${input.name}\0`);
  const sourceAddress = rawAllocate(sourceBytes);
  const nameAddress = rawAllocate(nameBytes);
  const headerBytes = header ? rawHeaderBundle(header.path, header.source) : null;
  const headerAddress = headerBytes ? rawAllocate(headerBytes) : 0;
  const outputCapacity = 256 * 1024;
  const outputAddress = Number(rawMalloc(BigInt(outputCapacity)));
  if (!outputAddress) throw new Error("raw adapter output allocation failed");
  try {
    return Number(rawAnalyzeExport(
      handle, sourceAddress, nameAddress,
      cursorOffset ?? Buffer.byteLength(input.source),
      headerAddress, headerBytes?.length ?? 0,
      128, 1024 * 1024, 4 * 1024 * 1024, 32,
      128, 1024 * 1024, 4 * 1024 * 1024, maxSymbols, 4096,
      4 * 1024 * 1024, 8 * 1024 * 1024,
      outputAddress, outputCapacity,
    ));
  } finally {
    rawFree(outputAddress);
    if (headerAddress) rawFree(headerAddress);
    rawFree(nameAddress);
    rawFree(sourceAddress);
  }
}

function rawCompileWat(handle, source) {
  const sourceAddress = rawAllocate(rawEncoder.encode(`${source}\0`));
  const outputCapacity = 512 * 1024;
  const outputAddress = Number(rawMalloc(BigInt(outputCapacity)));
  if (!outputAddress) throw new Error("raw compile output allocation failed");
  try {
    return Number(rawExports.agc_wasm_adapter_compile_wat(
      handle, sourceAddress, outputAddress, outputCapacity,
    ));
  } finally {
    rawFree(outputAddress);
    rawFree(sourceAddress);
  }
}

const rawHandle = Number(rawCreateExport());
if (!rawHandle) throw new Error("raw adapter creation failed");
try {
  const stableSource = { name: "stable.c", source: "int stable_value;" };
  for (const header of [
    { path: "first.h", source: "#define FIRST_HEADER 1\n" },
    { path: "second.h", source: "#define SECOND_HEADER 2\n" },
  ]) {
    const headerInput = {
      name: "raw-header.c",
      source: `#include <${header.path}>\nint value;`,
    };
    if (rawAnalyze(rawHandle, headerInput, 4096, null, header) < 0 ||
        Number(rawExports.agc_wasm_adapter_dependency_count(rawHandle)) !== 1) {
      throw new Error("raw adapter virtual dependency analysis failed");
    }
    const dependencyAddress = Number(
      rawExports.agc_wasm_adapter_dependency_name_ptr(rawHandle, 0),
    );
    if (!dependencyAddress || rawCString(dependencyAddress) !== header.path) {
      throw new Error("virtual dependency state leaked between analyses");
    }
  }
  if (rawAnalyze(
    rawHandle,
    { name: "raw-limit.c", source: "int first; int second;" },
    1,
  ) !== -7 || Number(rawGenerationExport(rawHandle)) !== 1 ||
      Number(rawExports.agc_wasm_adapter_dependency_count(rawHandle)) !== 0) {
    throw new Error("resource-limit failure retained stale translation-unit state");
  }
  for (let iteration = 0; iteration < 20; iteration++) {
    if (rawAnalyze(rawHandle, stableSource) < 0) {
      throw new Error("raw adapter warm-up analysis failed");
    }
  }
  if (Number(rawGenerationExport(rawHandle)) !== 1) {
    throw new Error("language analysis recreated its session during warm-up");
  }
  const warmPages = rawMemory.buffer.byteLength / 65536;
  for (let iteration = 0; iteration < 1000; iteration++) {
    if (rawAnalyze(rawHandle, stableSource) < 0) {
      throw new Error(`raw adapter repeated analysis failed at ${iteration}`);
    }
  }
  if (Number(rawGenerationExport(rawHandle)) !== 1) {
    throw new Error("language analysis recreated its session across repeated requests");
  }
  if (rawMemory.buffer.byteLength / 65536 !== warmPages) {
    throw new Error("Wasm memory pages grew after language-analysis warm-up");
  }
  if (rawAnalyze(rawHandle, stableSource, 4096,
                 Buffer.byteLength(stableSource.source) + 1) !== -1 ||
      Number(rawGenerationExport(rawHandle)) !== 1) {
    throw new Error("invalid cursor discarded a healthy language session");
  }
  if (rawCompileWat(rawHandle, "int main(void) { return 3; }") <= 0 ||
      Number(rawGenerationExport(rawHandle)) !== 2 ||
      rawAnalyze(rawHandle, stableSource) < 0 ||
      Number(rawGenerationExport(rawHandle)) !== 2) {
    throw new Error("analysis/compile/analysis session lifecycle is inconsistent");
  }
} finally {
  if (Number(rawDestroyExport(rawHandle)) !== 0) {
    throw new Error("raw adapter destruction failed");
  }
}

compiler.dispose();
try {
  analyzeAtEnd({ name: "disposed.c", source: "int disposed;" });
  throw new Error("disposed compiler accepted language analysis");
} catch (error) {
  if (error.message === "disposed compiler accepted language analysis") throw error;
}
console.log("wasm language analysis tests passed");
