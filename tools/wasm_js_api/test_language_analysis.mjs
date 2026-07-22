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

compiler.dispose();
console.log("wasm language analysis tests passed");
