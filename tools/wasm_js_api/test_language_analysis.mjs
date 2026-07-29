import { readFile } from "node:fs/promises";
import { execFileSync } from "node:child_process";
import assert from "node:assert/strict";
import {
  AgcResourceLimitError,
  createCompiler,
} from "./agc-wasm.js";

const wasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const nativeAnalysisPath = process.argv[3] || "build/test_language_analysis";
const wasmModule = await WebAssembly.compile(await readFile(wasmPath));
const compiler = await createCompiler(wasmModule);

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

const starterSource = {
  name: "starter.c",
  source: "#include <game.h>\n" +
    "enum { PLAYER_SIZE = 12 };\n" +
    "static int player_x;\n" +
    "static int player_y;\n" +
    "static void update(void) {\n" +
    "  if (player_x > GAME_SCREEN_WIDTH - PLAYER_SIZE) {\n" +
    "    player_x = GAME_SCREEN_WIDTH - PLAYER_SIZE;\n" +
    "  }\n" +
    "  if (player_y > GAME_SCREEN_HEIGHT - PLAYER_SIZE) {\n" +
    "    player_y = GAME_SCREEN_HEIGHT - PLAYER_SIZE;\n" +
    "  }\n" +
    "}\n",
};
const starterHeaders = {
  "game.h": "#define GAME_SCREEN_WIDTH 640\n" +
    "#define GAME_SCREEN_HEIGHT 360\n",
};
const starterHoverCases = [
  ["GAME_SCREEN_WIDTH", "640"],
  ["GAME_SCREEN_HEIGHT", "360"],
].flatMap(([name, replacement]) => {
  const start = starterSource.source.indexOf(name);
  return [0, Math.floor(name.length / 2), Buffer.byteLength(name)].map(
    (delta) => ({ name, replacement, byteOffset: start + delta }),
  );
});

function assertStarterMacroHover(toolchain, hoverCase, lifecycle) {
  const hoverResult = toolchain.analyzeSource(starterSource, {
    headers: starterHeaders,
    cursor: {
      sourceName: starterSource.name,
      byteOffset: hoverCase.byteOffset,
    },
  });
  const completion = symbol(hoverResult, hoverCase.name, "macro");
  if (hoverResult.hover?.name !== hoverCase.name ||
      hoverResult.hover.kind !== "macro" ||
      hoverResult.hover.macro?.replacement !== hoverCase.replacement ||
      hoverResult.hover.declaration.sourceName !== "game.h" ||
      completion?.macro?.replacement !== hoverCase.replacement) {
    throw new Error(
      `${lifecycle} starter macro hover failed: ${JSON.stringify(hoverResult)}`,
    );
  }
}

for (const hoverCase of starterHoverCases) {
  assertStarterMacroHover(compiler, hoverCase, "reused instance");
}
for (const hoverCase of starterHoverCases) {
  const freshCompiler = await createCompiler(wasmModule);
  try {
    assertStarterMacroHover(freshCompiler, hoverCase, "fresh instance");
  } finally {
    freshCompiler.dispose();
  }
}

const enumParitySource = {
  name: "main.c",
  source: "enum {\n" +
    "  PLAYER_SIZE = 12,\n" +
    "  PLAYER_SPEED = 2\n" +
    "};\n" +
    "int main(void) { return PLAYER_SIZE + PLAYER_SPEED; }\n",
};
const enumParityNameStart = enumParitySource.source.indexOf("PLAYER_SIZE");
const wasmEnumParity = compiler.analyzeSource(enumParitySource, {
  cursor: {
    sourceName: enumParitySource.name,
    byteOffset: enumParityNameStart + 4,
  },
});
const nativeEnumParity = JSON.parse(execFileSync(
  nativeAnalysisPath, ["--enum-parity-json"], { encoding: "utf8" },
));
assert.deepStrictEqual(
  wasmEnumParity,
  nativeEnumParity,
  "native and Wasm enum declaration hover snapshots differ",
);

const enumSource = {
  name: "enum-hover.c",
  source: "enum {\n" +
    "  PLAYER_ZERO,\n" +
    "  PLAYER_SIZE = 12,\n" +
    "  PLAYER_SPEED = 2,\n" +
    "  PLAYER_NEXT,\n" +
    "  PLAYER_EXPR = PLAYER_SIZE + 5\n" +
    "};\n" +
    "int main(void) {\n" +
    "  return PLAYER_ZERO + PLAYER_SIZE + PLAYER_SPEED + " +
    "PLAYER_NEXT + PLAYER_EXPR;\n" +
    "}\n",
};
const enumUseRegion = enumSource.source.indexOf("return ");
const enumCases = [
  ["PLAYER_ZERO", "0", false],
  ["PLAYER_SIZE", "12", true],
  ["PLAYER_SPEED", "2", true],
  ["PLAYER_NEXT", "3", false],
  ["PLAYER_EXPR", "17", false],
];
for (const [name, constantValue, checkAllPositions] of enumCases) {
  const declarationStart = enumSource.source.indexOf(name);
  const useStart = enumSource.source.indexOf(name, enumUseRegion);
  const useResult = compiler.analyzeSource(enumSource, {
    cursor: {
      sourceName: enumSource.name,
      byteOffset: useStart + Buffer.byteLength(name),
    },
  });
  const useHover = useResult.hover;
  if (useHover?.name !== name || useHover.kind !== "enumConstant" ||
      useHover.initializer.state !== "explicitConstant" ||
      useHover.initializer.constantValue !== constantValue) {
    throw new Error(`enum use hover failed: ${JSON.stringify(useResult)}`);
  }
  const cursorDeltas = checkAllPositions
    ? [0, Math.floor(name.length / 2), Buffer.byteLength(name)]
    : [Math.floor(name.length / 2)];
  for (const cursorDelta of cursorDeltas) {
    const declarationResult = compiler.analyzeSource(enumSource, {
      cursor: {
        sourceName: enumSource.name,
        byteOffset: declarationStart + cursorDelta,
      },
    });
    const declarationHover = declarationResult.hover;
    if (declarationHover) {
      assert.deepStrictEqual(
        declarationHover.declaration,
        useHover.declaration,
        `enum declaration range differs for ${name}`,
      );
    }
    if (declarationHover?.name !== name ||
        declarationHover.kind !== "enumConstant" ||
        declarationHover.initializer.state !== "explicitConstant" ||
        declarationHover.initializer.constantValue !== constantValue ||
        declarationHover.signature !== useHover.signature ||
        declarationHover.type !== useHover.type ||
        declarationResult.partial ||
        declarationResult.diagnostics.length !== 0) {
      throw new Error(
        `enum declaration hover failed: ${JSON.stringify(declarationResult)}`,
      );
    }
  }
}

const objectParitySource = {
  name: "main.c",
  source: "#include <game.h>\n" +
    "static int player_x;\n" +
    "\n" +
    "int main(void) {\n" +
    "  while (game_running()) {\n" +
    "    player_x++;\n" +
    "  }\n" +
    "  return 0;\n" +
    "}\n",
};
const objectParityHeaders = {
  "game.h": "int game_running(void);\n",
};
const objectDeclarationStart = objectParitySource.source.indexOf("player_x");
const objectUseStart = objectParitySource.source.indexOf(
  "player_x",
  objectDeclarationStart + "player_x".length,
);
const wasmObjectParity = compiler.analyzeSource(objectParitySource, {
  headers: objectParityHeaders,
  cursor: {
    sourceName: objectParitySource.name,
    byteOffset: objectDeclarationStart + 4,
  },
});
const nativeObjectParity = JSON.parse(execFileSync(
  nativeAnalysisPath, ["--object-parity-json"], { encoding: "utf8" },
));
assert.deepStrictEqual(
  wasmObjectParity,
  nativeObjectParity,
  "native and Wasm object declaration hover snapshots differ",
);

function objectHoverFields(hover) {
  return hover && {
    name: hover.name,
    kind: hover.kind,
    type: hover.type,
    signature: hover.signature,
    declaration: hover.declaration,
    initializer: hover.initializer,
  };
}

const objectUseResult = compiler.analyzeSource(objectParitySource, {
  headers: objectParityHeaders,
  cursor: {
    sourceName: objectParitySource.name,
    byteOffset: objectUseStart + "player_x".length,
  },
});
if (objectUseResult.hover?.name !== "player_x" ||
    objectUseResult.hover.kind !== "object" ||
    objectUseResult.hover.type !== "int" ||
    objectUseResult.hover.signature !== "static int player_x" ||
    objectUseResult.hover.initializer.state !== "zero") {
  throw new Error(
    `object use hover baseline failed: ${JSON.stringify(objectUseResult)}`,
  );
}
const objectCursorDeltas = [
  0,
  1,
  Math.floor("player_x".length / 2),
  Buffer.byteLength("player_x"),
];
for (const cursorDelta of objectCursorDeltas) {
  const declarationResult = compiler.analyzeSource(objectParitySource, {
    headers: objectParityHeaders,
    cursor: {
      sourceName: objectParitySource.name,
      byteOffset: objectDeclarationStart + cursorDelta,
    },
  });
  assert.deepStrictEqual(
    objectHoverFields(declarationResult.hover),
    objectHoverFields(objectUseResult.hover),
    `object declaration hover differs at byte ${cursorDelta}`,
  );
  if (declarationResult.partial ||
      declarationResult.diagnostics.length !== 0) {
    throw new Error(
      `complete object declaration was partial: ${JSON.stringify(declarationResult)}`,
    );
  }

  const alternatingUseResult = compiler.analyzeSource(objectParitySource, {
    headers: objectParityHeaders,
    cursor: {
      sourceName: objectParitySource.name,
      byteOffset: objectUseStart + cursorDelta,
    },
  });
  assert.deepStrictEqual(
    objectHoverFields(alternatingUseResult.hover),
    objectHoverFields(objectUseResult.hover),
    `object use hover leaked state at byte ${cursorDelta}`,
  );
}
for (const cursorDelta of objectCursorDeltas) {
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(objectParitySource, {
      headers: objectParityHeaders,
      cursor: {
        sourceName: objectParitySource.name,
        byteOffset: objectDeclarationStart + cursorDelta,
      },
    });
    assert.deepStrictEqual(
      objectHoverFields(freshResult.hover),
      objectHoverFields(objectUseResult.hover),
      `fresh object declaration hover differs at byte ${cursorDelta}`,
    );
    if (freshResult.partial || freshResult.diagnostics.length !== 0) {
      throw new Error(
        `fresh object declaration was partial: ${JSON.stringify(freshResult)}`,
      );
    }
  } finally {
    freshCompiler.dispose();
  }
}

const functionDefinitionSource = {
  name: "main.c",
  source: "#include <game.h>\n" +
    "static void move_and_draw(void) {}\n" +
    "int main(void) { move_and_draw(); return 0; }\n",
};
const functionDefinitionHeaders = { "game.h": "" };
const moveName = "move_and_draw";
const moveDefinitionStart =
  functionDefinitionSource.source.indexOf(moveName);
const moveUseStart = functionDefinitionSource.source.indexOf(
  moveName,
  moveDefinitionStart + moveName.length,
);
const functionCursorDeltas = [
  0,
  1,
  Math.floor(moveName.length / 2),
  Buffer.byteLength(moveName),
];
const wasmFunctionDefinitionParity = compiler.analyzeSource(
  functionDefinitionSource,
  {
    headers: functionDefinitionHeaders,
    cursor: {
      sourceName: functionDefinitionSource.name,
      byteOffset:
        moveDefinitionStart + Math.floor(moveName.length / 2),
    },
  },
);
const nativeFunctionDefinitionParity = JSON.parse(execFileSync(
  nativeAnalysisPath,
  ["--function-definition-parity-json"],
  { encoding: "utf8" },
));
assert.deepStrictEqual(
  wasmFunctionDefinitionParity,
  nativeFunctionDefinitionParity,
  "native and Wasm function definition hover snapshots differ",
);

function functionHoverFields(hover) {
  return hover && {
    name: hover.name,
    kind: hover.kind,
    type: hover.type,
    signature: hover.signature,
    storageClass: hover.storageClass,
    declaration: hover.declaration,
    function: hover.function,
  };
}

const functionUseResult = compiler.analyzeSource(
  functionDefinitionSource,
  {
    headers: functionDefinitionHeaders,
    cursor: {
      sourceName: functionDefinitionSource.name,
      byteOffset: moveUseStart + moveName.length,
    },
  },
);
if (functionUseResult.hover?.name !== moveName ||
    functionUseResult.hover.kind !== "function" ||
    functionUseResult.hover.storageClass !== "static" ||
    functionUseResult.hover.function?.returnType !== "void" ||
    functionUseResult.hover.function?.hasPrototype !== true ||
    functionUseResult.hover.function?.variadic !== false ||
    functionUseResult.hover.function?.parameters.length !== 0 ||
    functionUseResult.hover.declaration.start.offset !==
      moveDefinitionStart ||
    functionUseResult.partial ||
    functionUseResult.diagnostics.length !== 0) {
  throw new Error(
    `function use hover baseline failed: ${JSON.stringify(functionUseResult)}`,
  );
}
for (const cursorDelta of functionCursorDeltas) {
  const definitionResult = compiler.analyzeSource(
    functionDefinitionSource,
    {
      headers: functionDefinitionHeaders,
      cursor: {
        sourceName: functionDefinitionSource.name,
        byteOffset: moveDefinitionStart + cursorDelta,
      },
    },
  );
  assert.deepStrictEqual(
    functionHoverFields(definitionResult.hover),
    functionHoverFields(functionUseResult.hover),
    `function definition hover differs at byte ${cursorDelta}`,
  );
  if (definitionResult.partial ||
      definitionResult.diagnostics.length !== 0) {
    throw new Error(
      `complete function definition was partial: ${JSON.stringify(definitionResult)}`,
    );
  }

  const alternatingUseResult = compiler.analyzeSource(
    functionDefinitionSource,
    {
      headers: functionDefinitionHeaders,
      cursor: {
        sourceName: functionDefinitionSource.name,
        byteOffset: moveUseStart + cursorDelta,
      },
    },
  );
  assert.deepStrictEqual(
    functionHoverFields(alternatingUseResult.hover),
    functionHoverFields(functionUseResult.hover),
    `function use hover leaked state at byte ${cursorDelta}`,
  );
  if (alternatingUseResult.partial ||
      alternatingUseResult.diagnostics.length !== 0) {
    throw new Error(
      `complete function use was partial: ${JSON.stringify(alternatingUseResult)}`,
    );
  }
}
for (const cursorDelta of functionCursorDeltas) {
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDefinitionSource,
      {
        headers: functionDefinitionHeaders,
        cursor: {
          sourceName: functionDefinitionSource.name,
          byteOffset: moveDefinitionStart + cursorDelta,
        },
      },
    );
    assert.deepStrictEqual(
      functionHoverFields(freshResult.hover),
      functionHoverFields(functionUseResult.hover),
      `fresh function definition hover differs at byte ${cursorDelta}`,
    );
    if (freshResult.partial || freshResult.diagnostics.length !== 0) {
      throw new Error(
        `fresh function definition was partial: ${JSON.stringify(freshResult)}`,
      );
    }
  } finally {
    freshCompiler.dispose();
  }
}

const functionStorageSource = {
  name: "function-storage.c",
  source:
    "static void internal_function(void);\n" +
    "static void internal_function(void) {}\n" +
    "extern void external_function(void);\n" +
    "void external_function(void) {}\n" +
    "void default_function(void) {}\n" +
    "int main(void) {\n" +
    "  internal_function();\n" +
    "  external_function();\n" +
    "  default_function();\n" +
    "  return 0;\n" +
    "}\n",
};
const functionStorageCases = [
  { name: "internal_function", storageClass: "static", count: 3 },
  { name: "external_function", storageClass: "extern", count: 3 },
  { name: "default_function", storageClass: "", count: 2 },
];
for (const functionCase of functionStorageCases) {
  let occurrence = 0;
  for (let index = 0; index < functionCase.count; index++) {
    occurrence = functionStorageSource.source.indexOf(
      functionCase.name,
      occurrence,
    );
    assert.notEqual(
      occurrence,
      -1,
      `missing ${functionCase.name} occurrence ${index}`,
    );
    const storageResult = compiler.analyzeSource(functionStorageSource, {
      cursor: {
        sourceName: functionStorageSource.name,
        byteOffset: occurrence + 1,
      },
    });
    assert.equal(storageResult.hover?.name, functionCase.name);
    assert.equal(storageResult.hover?.kind, "function");
    assert.equal(
      storageResult.hover?.storageClass,
      functionCase.storageClass,
      `${functionCase.name} storage class differs at occurrence ${index}`,
    );
    assert.equal(storageResult.partial, false);
    assert.deepStrictEqual(storageResult.diagnostics, []);
    occurrence += functionCase.name.length;
  }
}

const objectDeclarationCases = [
  {
    name: "explicit_value",
    source: "static int explicit_value = 42;\n" +
      "int main(void) { return explicit_value; }\n",
    initializer: { state: "explicitConstant", constantValue: "42" },
    signature: "static int explicit_value",
  },
  {
    name: "global_value",
    source: "int global_value;\n" +
      "int main(void) { return global_value; }\n",
    initializer: { state: "zero", constantValue: null },
    signature: "int global_value",
  },
  {
    name: "local_value",
    source: "int main(void) { int local_value = 3; return local_value; }\n",
    initializer: { state: "runtime", constantValue: null },
    signature: "int local_value",
  },
  {
    name: "first_value",
    source: "int first_value = 1, second_value = 2;\n" +
      "int main(void) { return first_value + second_value; }\n",
    initializer: { state: "explicitConstant", constantValue: "1" },
    signature: "int first_value",
  },
  {
    name: "second_value",
    source: "int first_value = 1, second_value = 2;\n" +
      "int main(void) { return first_value + second_value; }\n",
    initializer: { state: "explicitConstant", constantValue: "2" },
    signature: "int second_value",
  },
  {
    name: "local_static",
    source: "int main(void) { static int local_static; return local_static; }\n",
    initializer: { state: "zero", constantValue: null },
    signature: "static int local_static",
  },
  {
    name: "local_extern",
    source: "int main(void) { extern int local_extern; return local_extern; }\n",
    initializer: { state: "none", constantValue: null },
    signature: "extern int local_extern",
  },
  {
    name: "local_register",
    source: "int main(void) { register int local_register = 1; " +
      "return local_register; }\n",
    initializer: { state: "runtime", constantValue: null },
    signature: "register int local_register",
  },
  {
    name: "score_pointer",
    source: "const int *score_pointer;\n" +
      "int main(void) { return score_pointer != 0; }\n",
    initializer: { state: "zero", constantValue: null },
    signature: "const int *score_pointer",
    reparseSignature: true,
  },
  {
    name: "volatile_score",
    source: "volatile int volatile_score;\n" +
      "int main(void) { return volatile_score; }\n",
    initializer: { state: "zero", constantValue: null },
    signature: "volatile int volatile_score",
  },
  {
    name: "scores",
    source: "int scores[4];\n" +
      "int main(void) { return scores[0]; }\n",
    initializer: { state: "zero", constantValue: null },
    signature: "int scores[4]",
    reparseSignature: true,
  },
  {
    name: "callback",
    source: "int (*callback)(int);\n" +
      "int main(void) { return callback ? callback(1) : 0; }\n",
    initializer: { state: "zero", constantValue: null },
    signature: "int (*callback)(int)",
    reparseSignature: true,
  },
  {
    name: "typed_score",
    source: "typedef int Score;\n" +
      "Score typed_score;\n" +
      "int main(void) { return typed_score; }\n",
    initializer: { state: "zero", constantValue: null },
    signature: "int typed_score",
    reparseSignature: true,
  },
];
function objectDisplayFields(hover) {
  return hover && {
    name: hover.name,
    kind: hover.kind,
    type: hover.type,
    signature: hover.signature,
    initializer: hover.initializer,
  };
}
for (const analysisCase of objectDeclarationCases) {
  const declarationSource = {
    name: "object-declaration.c",
    source: analysisCase.source,
  };
  const declarationStart = declarationSource.source.indexOf(analysisCase.name);
  const useStart = declarationSource.source.indexOf(
    analysisCase.name,
    declarationStart + analysisCase.name.length,
  );
  const useResult = compiler.analyzeSource(declarationSource, {
    cursor: {
      sourceName: declarationSource.name,
      byteOffset: useStart + Buffer.byteLength(analysisCase.name),
    },
  });
  const declarationResult = compiler.analyzeSource(declarationSource, {
    cursor: {
      sourceName: declarationSource.name,
      byteOffset: declarationStart + Math.floor(analysisCase.name.length / 2),
    },
  });
  assert.deepStrictEqual(
    objectDisplayFields(declarationResult.hover),
    objectDisplayFields(useResult.hover),
    `${analysisCase.name} declaration and use hover differ`,
  );
  if (declarationResult.hover?.initializer.state !==
        analysisCase.initializer.state ||
      declarationResult.hover?.initializer.constantValue !==
        analysisCase.initializer.constantValue ||
      declarationResult.hover?.signature !== analysisCase.signature ||
      declarationResult.partial ||
      declarationResult.diagnostics.length !== 0) {
    throw new Error(
      `object declaration form failed: ${JSON.stringify(declarationResult)}`,
    );
  }
  if (analysisCase.reparseSignature) {
    const replaySource = {
      name: "object-signature-replay.c",
      source: `${declarationResult.hover.signature};\n`,
    };
    const replayNameStart = replaySource.source.indexOf(analysisCase.name);
    const replayResult = compiler.analyzeSource(replaySource, {
      cursor: {
        sourceName: replaySource.name,
        byteOffset: replayNameStart + Math.floor(analysisCase.name.length / 2),
      },
    });
    if (replayResult.hover?.kind !== "object" ||
        replayResult.hover.name !== analysisCase.name ||
        replayResult.hover.signature !== analysisCase.signature ||
        replayResult.partial ||
        replayResult.diagnostics.length !== 0) {
      throw new Error(
        `object signature did not reparse: ${JSON.stringify(replayResult)}`,
      );
    }
  }
}

const declarationFreeHeaders = {
  "game.h": "#define GAME_SCREEN_WIDTH 640\n" +
    "int game_running(void);\n",
};
const declarationFreeCases = [
  { name: "empty", source: "" },
  { name: "normal", source: "int value;\n" },
  { name: "incomplete", source: "int" },
  { name: "include", source: "#include <game.h>\n" },
  { name: "comment", source: "/* comment only */\n" },
  { name: "whitespace", source: "\n" },
  { name: "define", source: "#define LOCAL_VALUE 1\n" },
  {
    name: "include-and-declaration",
    source: "#include <game.h>\n\nint value;\n",
  },
];
for (const analysisCase of declarationFreeCases) {
  const declarationFreeSource = {
    name: "aab/a.c",
    source: analysisCase.source,
  };
  const declarationFreeResult = compiler.analyzeSource(declarationFreeSource, {
    headers: declarationFreeHeaders,
    diagnosticLocale: "ja",
    cursor: {
      sourceName: declarationFreeSource.name,
      byteOffset: Buffer.byteLength(declarationFreeSource.source),
    },
  });
  if (analysisCase.name === "incomplete") {
    const partialIdentifier = declarationFreeResult.diagnostics.find(
      (diagnostic) => diagnostic.code === "AGC_PARTIAL_IDENTIFIER",
    );
    if (!declarationFreeResult.partial ||
        !partialIdentifier ||
        partialIdentifier.start.offset !== 0 ||
        partialIdentifier.end.offset !== 3) {
      throw new Error(
        `incomplete declaration lost diagnostics: ${JSON.stringify(declarationFreeResult)}`,
      );
    }
    continue;
  }
  if (declarationFreeResult.partial ||
      declarationFreeResult.diagnostics.length !== 0) {
    throw new Error(
      `declaration-free source was diagnosed: ${JSON.stringify({
        name: analysisCase.name,
        result: declarationFreeResult,
      })}`,
    );
  }
  if (analysisCase.name === "define" &&
      !symbol(declarationFreeResult, "LOCAL_VALUE", "macro")) {
    throw new Error("define-only analysis lost its macro completion");
  }
  if (analysisCase.name === "include" &&
      (symbol(
        declarationFreeResult,
        "GAME_SCREEN_WIDTH",
        "macro",
      )?.macro?.replacement !== "640" ||
       !symbol(declarationFreeResult, "game_running", "function") ||
       JSON.stringify(declarationFreeResult.dependencies) !==
         JSON.stringify(["game.h"]))) {
    throw new Error(
      `include-only analysis lost virtual header state: ${JSON.stringify(declarationFreeResult)}`,
    );
  }
}

const implicitIntSource = { name: "aab/a.c", source: "value;" };
const implicitIntResult = compiler.analyzeSource(implicitIntSource, {
  headers: declarationFreeHeaders,
  diagnosticLocale: "ja",
  cursor: {
    sourceName: implicitIntSource.name,
    byteOffset: Buffer.byteLength(implicitIntSource.source),
  },
});
const implicitIntDiagnostic = implicitIntResult.diagnostics.find(
  (diagnostic) => diagnostic.code === "E3088",
);
if (!implicitIntResult.partial || !implicitIntDiagnostic ||
    implicitIntDiagnostic.start.offset !== 0 ||
    implicitIntDiagnostic.end.offset !== 5) {
  throw new Error(
    `real implicit-int declaration lost E3088: ${JSON.stringify(implicitIntResult)}`,
  );
}

const includeOnlyParitySource = {
  name: "aab/a.c",
  source: "#include <game.h>\n\n",
};
const wasmIncludeOnlyParity = compiler.analyzeSource(includeOnlyParitySource, {
  headers: declarationFreeHeaders,
  diagnosticLocale: "ja",
  cursor: {
    sourceName: includeOnlyParitySource.name,
    byteOffset: Buffer.byteLength(includeOnlyParitySource.source),
  },
});
const nativeIncludeOnlyParity = JSON.parse(execFileSync(
  nativeAnalysisPath, ["--include-only-parity-json"], { encoding: "utf8" },
));
assert.deepStrictEqual(
  wasmIncludeOnlyParity,
  nativeIncludeOnlyParity,
  "native and Wasm include-only analysis snapshots differ",
);

const failingAnalysisCompiler = await createCompiler(wasmModule);
try {
  const failingSource = {
    name: "failing-analysis.c",
    source: "#include <game.h>\n" +
      "int main(void) { int = ; GAME_SCREEN_WIDTH",
  };
  try {
    const failureSnapshot = failingAnalysisCompiler.analyzeSource(failingSource, {
      headers: starterHeaders,
      cursor: {
        sourceName: failingSource.name,
        byteOffset: Buffer.byteLength(failingSource.source),
      },
    });
    if (!failureSnapshot.partial || !Array.isArray(failureSnapshot.diagnostics)) {
      throw new Error(
        `invalid analysis source was not partial: ${JSON.stringify(failureSnapshot)}`,
      );
    }
  } catch (error) {
    if (error instanceof WebAssembly.RuntimeError ||
        error.name !== "AgcLanguageAnalysisError" ||
        error.code !== "AGC_LANGUAGE_ANALYSIS_FAILED" ||
        !Array.isArray(error.diagnostics)) {
      throw error;
    }
  }
} finally {
  failingAnalysisCompiler.dispose();
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
