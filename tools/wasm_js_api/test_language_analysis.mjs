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

const documentationSource = {
  name: "documentation.c",
  source:
    "/** 敵の現在位置 */\n" +
    "static int enemy_x;\n" +
    "\n" +
    "/// 歩行中の画像番号を返す\n" +
    "/// alternateが0以外なら第一フレーム\n" +
    "static int walk_frame(int alternate) {\n" +
    "  return alternate ? 1 : 2;\n" +
    "}\n" +
    "\n" +
    "/**\n" +
    " * 読み取り専用の値\n" +
    " *\n" +
    " * 日本語の段落を維持する\n" +
    " */\n" +
    "static const int qualified_value = 3;\n" +
    "\n" +
    "/** 左右の座標 */\n" +
    "int left_value, right_value;\n" +
    "\n" +
    "/** 外部オブジェクト */\n" +
    "extern int external_value;\n" +
    "\n" +
    "/** prototype only */\n" +
    "int prototype_only(int value);\n" +
    "\n" +
    "/** definition only */\n" +
    "int definition_only(int value) { return value; }\n" +
    "\n" +
    "/** prototype wins */\n" +
    "int documented_both(int value);\n" +
    "/** definition loses */\n" +
    "int documented_both(int value) { return value; }\n" +
    "\n" +
    "int fallback_definition(int value);\n" +
    "/** definition fallback */\n" +
    "int fallback_definition(int value) { return value; }\n" +
    "\n" +
    "/** 空行で切れる */\n" +
    "\n" +
    "int blank_gap;\n" +
    "/** directiveで切れる */\n" +
    "#define DOCUMENTATION_BREAK 1\n" +
    "int directive_gap;\n" +
    "#define DOCUMENTATION_CONTINUATION \\\r\n" +
    "/** macro continuation */ 1\r\n" +
    "int directive_continuation_gap;\n" +
    "/** 最初の宣言だけ */\n" +
    "int first_only;\n" +
    "int declaration_after;\n" +
    "/* 通常block comment */\n" +
    "int ordinary_block;\n" +
    "// 通常line comment\n" +
    "int ordinary_line;\n" +
    "const char *comment_text = \"/** fake */\";\n" +
    "int string_after;\n" +
    "int comment_character = '/';\n" +
    "int character_after;\n" +
    "\n" +
    "/**\r\n" +
    "\t * CRLFの説明\r\n" +
    "\t * 二行目\r\n" +
    "\t */\r\n" +
    "static const int crlf_value = 5;\n" +
    "\n" +
    "int documentation_main(void) {\n" +
    "  /** local object */\n" +
    "  int local_value = 1;\n" +
    "  enemy_x = walk_frame(local_value);\n" +
    "  return enemy_x + qualified_value + left_value + right_value +\n" +
    "         external_value + prototype_only(local_value) +\n" +
    "         definition_only(local_value) + documented_both(local_value) +\n" +
    "         fallback_definition(local_value) + crlf_value;\n" +
    "}\n",
};

function documentationByteOffset(index) {
  return Buffer.byteLength(documentationSource.source.slice(0, index));
}

function assertDocumentation(result, documentationCase, lifecycle) {
  const hover = result.hover;
  const commentIndex = documentationSource.source.indexOf(
    documentationCase.comment,
  );
  assert.notEqual(commentIndex, -1, `${lifecycle} comment anchor`);
  const expectedRange = documentationCase.documentation
    ? {
      sourceName: documentationSource.name,
      start: documentationByteOffset(commentIndex),
      end: documentationByteOffset(
        commentIndex + documentationCase.comment.length,
      ),
    }
    : null;
  if (hover?.name !== documentationCase.name ||
      hover.kind !== documentationCase.kind ||
      hover.documentation !== documentationCase.documentation ||
      (expectedRange === null && hover.documentationRange !== null) ||
      (expectedRange !== null &&
       (hover.documentationRange?.sourceName !== expectedRange.sourceName ||
        hover.documentationRange.start.offset !== expectedRange.start ||
        hover.documentationRange.end.offset !== expectedRange.end))) {
    throw new Error(
      `${lifecycle} documentation hover failed: ${JSON.stringify(result)}`,
    );
  }
  const completion = symbol(result, documentationCase.name,
    documentationCase.kind);
  if (completion?.documentation !== documentationCase.documentation) {
    throw new Error(
      `${lifecycle} completion documentation differs: ${JSON.stringify(result)}`,
    );
  }
  if (!Object.isFrozen(result) || !Object.isFrozen(hover) ||
      (hover.documentationRange !== null &&
       (!Object.isFrozen(hover.documentationRange) ||
        !Object.isFrozen(hover.documentationRange.start)))) {
    throw new Error(`${lifecycle} documentation snapshot is mutable`);
  }
}

const documentationCases = [
  { name: "enemy_x", kind: "object", documentation: "敵の現在位置",
    comment: "/** 敵の現在位置 */" },
  { name: "walk_frame", kind: "function",
    documentation: "歩行中の画像番号を返す\nalternateが0以外なら第一フレーム",
    comment: "/// 歩行中の画像番号を返す\n" +
      "/// alternateが0以外なら第一フレーム" },
  { name: "qualified_value", kind: "object",
    documentation: "読み取り専用の値\n\n日本語の段落を維持する",
    comment: "/**\n * 読み取り専用の値\n *\n * 日本語の段落を維持する\n */" },
  { name: "left_value", kind: "object", documentation: "左右の座標",
    comment: "/** 左右の座標 */" },
  { name: "right_value", kind: "object", documentation: "左右の座標",
    comment: "/** 左右の座標 */" },
  { name: "external_value", kind: "object",
    documentation: "外部オブジェクト", comment: "/** 外部オブジェクト */" },
  { name: "prototype_only", kind: "function",
    documentation: "prototype only", comment: "/** prototype only */" },
  { name: "definition_only", kind: "function",
    documentation: "definition only", comment: "/** definition only */" },
  { name: "documented_both", kind: "function",
    documentation: "prototype wins", comment: "/** prototype wins */" },
  { name: "fallback_definition", kind: "function",
    documentation: "definition fallback",
    comment: "/** definition fallback */" },
  { name: "first_only", kind: "object", documentation: "最初の宣言だけ",
    comment: "/** 最初の宣言だけ */" },
  { name: "crlf_value", kind: "object",
    documentation: "CRLFの説明\n二行目",
    comment: "/**\r\n\t * CRLFの説明\r\n\t * 二行目\r\n\t */" },
  { name: "local_value", kind: "object", documentation: "local object",
    comment: "/** local object */" },
];

const nativeDocumentationSnapshots = new Map();
for (const documentationCase of documentationCases) {
  const useIndex = documentationSource.source.lastIndexOf(
    documentationCase.name,
  );
  assert.notEqual(useIndex, -1, `missing ${documentationCase.name} use`);
  const byteOffset = documentationByteOffset(useIndex) +
    Math.floor(Buffer.byteLength(documentationCase.name) / 2);
  const wasmResult = compiler.analyzeSource(documentationSource, {
    cursor: { sourceName: documentationSource.name, byteOffset },
  });
  assertDocumentation(wasmResult, documentationCase, "reused instance");
  const nativeResult = JSON.parse(execFileSync(
    nativeAnalysisPath,
    ["--documentation-hover-parity-json", String(byteOffset)],
    { encoding: "utf8" },
  ));
  assert.deepStrictEqual(
    wasmResult,
    nativeResult,
    `native and Wasm documentation snapshots differ for ${documentationCase.name}`,
  );
  nativeDocumentationSnapshots.set(byteOffset, nativeResult);
}

for (const documentationCase of documentationCases.slice(0, 2)) {
  const declarationIndex = documentationSource.source.indexOf(
    documentationCase.name,
  );
  const useIndex = documentationSource.source.lastIndexOf(
    documentationCase.name,
  );
  for (const occurrence of [declarationIndex, useIndex]) {
    for (const delta of [
      0,
      Math.floor(Buffer.byteLength(documentationCase.name) / 2),
      Buffer.byteLength(documentationCase.name),
    ]) {
      const byteOffset = documentationByteOffset(occurrence) + delta;
      const wasmResult = compiler.analyzeSource(documentationSource, {
        cursor: { sourceName: documentationSource.name, byteOffset },
      });
      assertDocumentation(
        wasmResult, documentationCase, "reused declaration/use instance",
      );
      const nativeResult = JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--documentation-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      ));
      assert.deepStrictEqual(
        wasmResult,
        nativeResult,
        `native and Wasm documentation snapshots differ at byte ${byteOffset}`,
      );
      nativeDocumentationSnapshots.set(byteOffset, nativeResult);
    }
  }
}

for (const documentationCase of documentationCases.slice(0, 2)) {
  const useIndex = documentationSource.source.lastIndexOf(
    documentationCase.name,
  );
  const byteOffset = documentationByteOffset(useIndex) +
    Math.floor(Buffer.byteLength(documentationCase.name) / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(documentationSource, {
      cursor: { sourceName: documentationSource.name, byteOffset },
    });
    assertDocumentation(freshResult, documentationCase, "fresh instance");
    assert.deepStrictEqual(
      freshResult,
      nativeDocumentationSnapshots.get(byteOffset),
      `fresh documentation snapshot differs for ${documentationCase.name}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

for (const name of [
  "blank_gap", "directive_gap", "directive_continuation_gap",
  "declaration_after", "ordinary_block", "ordinary_line", "comment_text",
  "string_after", "comment_character", "character_after",
  "documentation_main",
]) {
  const index = documentationSource.source.indexOf(name);
  const byteOffset = documentationByteOffset(index) + 1;
  const undocumentedResult = compiler.analyzeSource(documentationSource, {
    cursor: { sourceName: documentationSource.name, byteOffset },
  });
  if (undocumentedResult.hover?.name !== name ||
      undocumentedResult.hover.documentation !== "" ||
      undocumentedResult.hover.documentationRange !== null) {
    throw new Error(
      `non-documentation comment leaked for ${name}: ${JSON.stringify(undocumentedResult)}`,
    );
  }
}

try {
  compiler.analyzeSource(
    { name: "d.c", source: "/** 12345678901234 */\nint x;\n" },
    {
      cursor: { sourceName: "d.c", byteOffset: 28 },
      limits: { maxAnalysisStringBytes: 13 },
    },
  );
  throw new Error("documentation string limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES" ||
      error.limit !== "maxAnalysisStringBytes" || error.max !== 13 ||
      error.actual !== 14) {
    throw error;
  }
}
const documentationAfterLimit = compiler.analyzeSource(
  { name: "d.c", source: "int x;" },
  { cursor: { sourceName: "d.c", byteOffset: 5 } },
);
assert.equal(documentationAfterLimit.hover?.documentation, "");

function documentationSnapshotSucceeds(input, maxAnalysisSnapshotBytes) {
  try {
    compiler.analyzeSource(input, {
      cursor: {
        sourceName: input.name,
        byteOffset: Buffer.byteLength(input.source),
      },
      limits: { maxAnalysisSnapshotBytes },
    });
    return true;
  } catch (error) {
    if (!(error instanceof AgcResourceLimitError) ||
        error.code !== "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES" ||
        error.limit !== "maxAnalysisSnapshotBytes") {
      throw error;
    }
    return false;
  }
}

function minimumDocumentationSnapshotLimit(input) {
  let low = 1;
  let high = 64 * 1024;
  assert.equal(documentationSnapshotSucceeds(input, high), true);
  while (low < high) {
    const middle = low + Math.floor((high - low) / 2);
    if (documentationSnapshotSucceeds(input, middle)) high = middle;
    else low = middle + 1;
  }
  return low;
}

const plainSnapshotInput = { name: "s.c", source: "int bounded;\n" };
const documentedSnapshotInput = {
  name: "s.c",
  source: "/** bounded doc */\nint bounded;\n",
};
const plainSnapshotMinimum = minimumDocumentationSnapshotLimit(
  plainSnapshotInput,
);
const documentedSnapshotMinimum = minimumDocumentationSnapshotLimit(
  documentedSnapshotInput,
);
assert.ok(documentedSnapshotMinimum > plainSnapshotMinimum,
  "documentation did not contribute to the Wasm snapshot byte limit");
assert.equal(documentationSnapshotSucceeds(
  plainSnapshotInput, documentedSnapshotMinimum - 1,
), true);
assert.equal(documentationSnapshotSucceeds(
  documentedSnapshotInput, documentedSnapshotMinimum - 1,
), false);
assert.equal(documentationSnapshotSucceeds(
  plainSnapshotInput, documentedSnapshotMinimum - 1,
), true, "Wasm session was not reusable after documentation snapshot limit");

const documentationProjectCompiler = await createCompiler(wasmModule);
try {
  const projectCallSource = {
    name: "main.c",
    source: "#include \"player.h\"\n" +
      "void update(void) { update_player(); }\n",
  };
  const definitionV1 = {
    name: "player.c",
    source: "#include \"player.h\"\n" +
      "/** definition v1 */\nvoid update_player(void) {}\n",
  };
  const definitionV2 = {
    name: "player.c",
    source: "#include \"player.h\"\n" +
      "/** definition v2 */\nvoid update_player(void) {}\n",
  };
  const callIndex = projectCallSource.source.lastIndexOf("update_player");
  const projectCases = [
    {
      revision: 1,
      headers: {
        "player.h": "/** header prototype */\nvoid update_player(void);\n",
      },
      sources: [definitionV1, projectCallSource],
      documentation: "header prototype",
      sourceName: "player.h",
    },
    {
      revision: 2,
      headers: { "player.h": "void update_player(void);\n" },
      sources: [definitionV1, projectCallSource],
      documentation: "definition v1",
      sourceName: "player.c",
    },
    {
      revision: 3,
      headers: { "player.h": "void update_player(void);\n" },
      sources: [definitionV2, projectCallSource],
      documentation: "definition v2",
      sourceName: "player.c",
    },
    {
      revision: 4,
      headers: { "player.h": "void update_player(void);\n" },
      sources: [projectCallSource],
      documentation: "",
      sourceName: null,
    },
  ];
  for (const projectCase of projectCases) {
    const projectResult = documentationProjectCompiler.analyzeProjectSource(
      projectCallSource,
      {
        projectRevision: projectCase.revision,
        projectSources: projectCase.sources,
        headers: projectCase.headers,
        cursor: {
          sourceName: projectCallSource.name,
          byteOffset: callIndex + 3,
        },
      },
    );
    if (projectResult.hover?.name !== "update_player" ||
        projectResult.hover.documentation !== projectCase.documentation ||
        (projectCase.sourceName === null
          ? projectResult.hover.documentationRange !== null
          : projectResult.hover.documentationRange?.sourceName !==
              projectCase.sourceName)) {
      throw new Error(
        `project documentation revision ${projectCase.revision} failed: ` +
        JSON.stringify(projectResult),
      );
    }
    const nativeProjectResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--documentation-project-parity-json", String(projectCase.revision)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      projectResult,
      nativeProjectResult,
      `native and Wasm project documentation snapshots differ at revision ${projectCase.revision}`,
    );
    if (projectCase.revision === 1) {
      const headerSource = {
        name: "player.h",
        source: projectCase.headers["player.h"],
      };
      const definitionIndex = definitionV1.source.indexOf("update_player");
      for (const [input, index] of [
        [headerSource, headerSource.source.indexOf("update_player")],
        [definitionV1, definitionIndex],
      ]) {
        for (const delta of [0, 6, "update_player".length]) {
          const locationResult =
            documentationProjectCompiler.analyzeProjectSource(input, {
              projectRevision: 1,
              projectSources: projectCase.sources,
              headers: projectCase.headers,
              cursor: {
                sourceName: input.name,
                byteOffset: index + delta,
              },
            });
          if (locationResult.hover?.documentation !== "header prototype" ||
              locationResult.hover.documentationRange?.sourceName !==
                "player.h") {
            throw new Error(
              `project documentation location failed: ${JSON.stringify(locationResult)}`,
            );
          }
        }
      }
    }
  }
  const visibleSource = {
    name: "visible.c",
    source: "/** visible prototype */\nvoid update_player(void);\n" +
      "void update(void) { update_player(); }\n",
  };
  const visibleResult = documentationProjectCompiler.analyzeProjectSource(
    visibleSource,
    {
      projectRevision: 5,
      projectSources: [definitionV1, visibleSource],
      headers: {
        "player.h":
          "/** project prototype */\nvoid update_player(void);\n",
      },
      cursor: {
        sourceName: visibleSource.name,
        byteOffset: visibleSource.source.lastIndexOf("update_player") + 3,
      },
    },
  );
  if (visibleResult.hover?.documentation !== "visible prototype" ||
      visibleResult.hover.documentationRange?.sourceName !== "visible.c") {
    throw new Error(
      `visible prototype documentation lost: ${JSON.stringify(visibleResult)}`,
    );
  }
} finally {
  documentationProjectCompiler.dispose();
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

const forControlSource = {
  name: "for-control.c",
  source: "/* 日本語 */\n" +
    "#define LOOP_LIMIT 8\n" +
    "enum { ENEMY_COUNT = 8 };\n" +
    "int identity(int value) { return value; }\n" +
    "int main(void) {\n" +
    "  int outer = 1;\n" +
    "  for (\n" +
    "#define LOOP_SEED 0; for (;;);\n" +
    "       outer = identity(\"for (;;);\"[0] + " +
    "/* for (;;); */ outer); // for (;;);\n" +
    "       outer < ENEMY_COUNT && outer < LOOP_LIMIT;\n" +
    "       outer++, outer += 0) {}\n" +
    "  for (;;) { outer += 0; break; }\n" +
    "  for (outer = 0; outer < 1;) { outer++; }\n" +
    "  for (int inner = 0; inner < ENEMY_COUNT; inner++) {\n" +
    "    for (int nested = 0; nested < inner; nested++) {\n" +
    "      outer += inner;\n" +
    "    }\n" +
    "  }\n" +
    "  outer += LOOP_LIMIT;\n" +
    "  return outer;\n" +
    "}\n",
};

function findForControlName(name, from) {
  const index = forControlSource.source.indexOf(name, from);
  assert.notEqual(index, -1, `missing for-control anchor: ${name}`);
  return index;
}

const firstFor = findForControlName("for (\n#define LOOP_SEED", 0);
const initOuter = findForControlName("outer", firstFor);
const nestedInitOuter = findForControlName("outer", initOuter + "outer".length);
const conditionOuter = findForControlName(
  "outer", nestedInitOuter + "outer".length,
);
const forEnumUse = findForControlName("ENEMY_COUNT", conditionOuter);
const forMacroUse = findForControlName("LOOP_LIMIT", forEnumUse);
const updateOuter = findForControlName("outer", forMacroUse);
const commaUpdateOuter = findForControlName(
  "outer", updateOuter + "outer".length,
);
const emptyForBodyOuter = findForControlName(
  "outer", commaUpdateOuter + "outer".length,
);
const emptyUpdateFor = findForControlName(
  "for (outer", emptyForBodyOuter + "outer".length,
);
const emptyUpdateInitOuter = findForControlName("outer", emptyUpdateFor);
const emptyUpdateConditionOuter = findForControlName(
  "outer", emptyUpdateInitOuter + "outer".length,
);
const innerFor = findForControlName("for (int inner", emptyUpdateConditionOuter);
const innerDeclaration = findForControlName("inner", innerFor);
const innerCondition = findForControlName(
  "inner", innerDeclaration + "inner".length,
);
const innerEnumUse = findForControlName("ENEMY_COUNT", innerCondition);
const innerUpdate = findForControlName("inner", innerEnumUse);
const nestedFor = findForControlName("for (int nested", innerUpdate);
const nestedConditionInner = findForControlName("inner", nestedFor);
const nestedBodyOuter = findForControlName("outer", nestedConditionInner);
const nestedBodyInner = findForControlName("inner", nestedBodyOuter);
const afterLoopOuter = findForControlName(
  "outer", nestedBodyInner + "inner".length,
);

const forControlHoverCases = [
  { name: "outer", kind: "object", index: initOuter },
  { name: "outer", kind: "object", index: nestedInitOuter },
  { name: "outer", kind: "object", index: conditionOuter },
  { name: "ENEMY_COUNT", kind: "enumConstant", index: forEnumUse },
  { name: "LOOP_LIMIT", kind: "macro", index: forMacroUse },
  { name: "outer", kind: "object", index: updateOuter },
  { name: "outer", kind: "object", index: commaUpdateOuter },
  { name: "outer", kind: "object", index: emptyForBodyOuter },
  { name: "outer", kind: "object", index: emptyUpdateInitOuter },
  { name: "outer", kind: "object", index: emptyUpdateConditionOuter },
  { name: "inner", kind: "object", index: innerCondition },
  { name: "ENEMY_COUNT", kind: "enumConstant", index: innerEnumUse },
  { name: "inner", kind: "object", index: innerUpdate },
  { name: "inner", kind: "object", index: nestedConditionInner },
  { name: "outer", kind: "object", index: nestedBodyOuter },
  { name: "inner", kind: "object", index: nestedBodyInner },
  { name: "outer", kind: "object", index: afterLoopOuter },
];
const forControlDeclarations = new Map();
const nativeForControlSnapshots = new Map();

function forControlByteOffset(analysisCase, delta) {
  return Buffer.byteLength(
    forControlSource.source.slice(0, analysisCase.index),
  ) + delta;
}

function assertForControlHover(result, analysisCase, lifecycle) {
  const hover = result.hover;
  if (result.partial || result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== analysisCase.kind ||
      (analysisCase.kind === "object" && hover.type !== "int") ||
      (analysisCase.kind === "enumConstant" &&
       hover.initializer.constantValue !== "8") ||
      (analysisCase.kind === "macro" && hover.macro?.replacement !== "8")) {
    throw new Error(
      `${lifecycle} for-control hover failed: ${JSON.stringify(result)}`,
    );
  }
  const declaration = forControlDeclarations.get(analysisCase.name);
  if (declaration) {
    assert.deepStrictEqual(
      hover.declaration,
      declaration,
      `${lifecycle} ${analysisCase.name} declaration range differs`,
    );
  } else {
    forControlDeclarations.set(analysisCase.name, hover.declaration);
  }
  if (analysisCase.index === afterLoopOuter &&
      symbol(result, "inner", "object")) {
    throw new Error(
      `${lifecycle} for-init object leaked after loop: ${JSON.stringify(result)}`,
    );
  }
}

for (const analysisCase of forControlHoverCases) {
  for (const delta of [
    0,
    Math.floor(analysisCase.name.length / 2),
    Buffer.byteLength(analysisCase.name),
  ]) {
    const byteOffset = forControlByteOffset(analysisCase, delta);
    const wasmResult = compiler.analyzeSource(forControlSource, {
      cursor: { sourceName: forControlSource.name, byteOffset },
    });
    assertForControlHover(wasmResult, analysisCase, "reused instance");
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--for-control-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm for-control snapshots differ at byte ${byteOffset}`,
    );
    nativeForControlSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of [
  forControlHoverCases[1],
  forControlHoverCases[3],
  forControlHoverCases[4],
  forControlHoverCases[10],
  forControlHoverCases[16],
]) {
  const delta = Math.floor(analysisCase.name.length / 2);
  const byteOffset = forControlByteOffset(analysisCase, delta);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(forControlSource, {
      cursor: { sourceName: forControlSource.name, byteOffset },
    });
    assertForControlHover(freshResult, analysisCase, "fresh instance");
    assert.deepStrictEqual(
      freshResult,
      nativeForControlSnapshots.get(byteOffset),
      `fresh native/Wasm for-control snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

const conditionalSource = {
  name: "conditional.c",
  source: "/* 日本語 */\n" +
    "#define CHOICE_MACRO 7\n" +
    "#if 1 ? 1 : 0\n" +
    "#define ACTIVE_BRANCH 1\n" +
    "#endif\n" +
    "enum { FIRST = 1, SECOND = 2, THIRD = 3 };\n" +
    "struct Flags { unsigned ready : 1; };\n" +
    "int choose(int left, ...) { return left; }\n" +
    "int frame(int alternate, int other) {\n" +
    "  int local = 4;\n" +
    "  const char *text = \"? : \\\"quoted\\\"\";\n" +
    "  // ? : ignored\n" +
    "  /* ? : ignored */\n" +
    "  switch (alternate) { case 0: break; default: break; }\n" +
    "conditional_label:\n" +
    "  local += choose(alternate ? FIRST : SECOND, " +
    "other ? SECOND : THIRD);\n" +
    "  local += (alternate ? FIRST : SECOND);\n" +
    "  int values[2] = { FIRST, SECOND };\n" +
    "  local += values[alternate ? 0 : 1];\n" +
    "  local += alternate ? (choose(local, FIRST), SECOND) :\n" +
    "                       (choose(local, SECOND), THIRD);\n" +
    "  local += alternate ? other ? FIRST : SECOND : THIRD;\n" +
    "  local += alternate ? FIRST : other ? SECOND : THIRD;\n" +
    "  return alternate ? other ? FIRST : SECOND :\n" +
    "         local ? CHOICE_MACRO : THIRD;\n" +
    "}\n",
};

function findConditionalName(name, from) {
  const index = conditionalSource.source.indexOf(name, from);
  assert.notEqual(index, -1, `missing conditional anchor: ${name}`);
  return index;
}

const conditionalDirect = findConditionalName(
  "choose(alternate ? FIRST : SECOND", 0,
);
const conditionalDirectAlternate = findConditionalName(
  "alternate", conditionalDirect,
);
const conditionalDirectFirst = findConditionalName(
  "FIRST", conditionalDirectAlternate,
);
const conditionalDirectSecond = findConditionalName(
  "SECOND", conditionalDirectFirst,
);
const conditionalDirectOther = findConditionalName(
  "other ?", conditionalDirectSecond,
);
const conditionalParenthesized = findConditionalName(
  "local += (alternate", conditionalDirectOther,
);
const conditionalParenthesizedAlternate = findConditionalName(
  "alternate", conditionalParenthesized,
);
const conditionalSubscript = findConditionalName(
  "values[alternate", conditionalParenthesizedAlternate,
);
const conditionalSubscriptAlternate = findConditionalName(
  "alternate", conditionalSubscript,
);
const conditionalComma = findConditionalName(
  "local += alternate ? (choose(local, FIRST), SECOND)",
  conditionalSubscriptAlternate,
);
const conditionalCommaAlternate = findConditionalName(
  "alternate", conditionalComma,
);
const conditionalCommaLocal = findConditionalName(
  "local", conditionalCommaAlternate,
);
const conditionalCommaFirst = findConditionalName(
  "FIRST", conditionalCommaLocal,
);
const conditionalCommaSecond = findConditionalName(
  "SECOND", conditionalCommaFirst,
);
const conditionalCommaFalseLocal = findConditionalName(
  "local", conditionalCommaSecond,
);
const conditionalCommaThird = findConditionalName(
  "THIRD", conditionalCommaFalseLocal,
);
const conditionalNestedTrue = findConditionalName(
  "local += alternate ? other ? FIRST : SECOND : THIRD",
  conditionalCommaThird,
);
const conditionalNestedTrueAlternate = findConditionalName(
  "alternate", conditionalNestedTrue,
);
const conditionalNestedTrueOther = findConditionalName(
  "other", conditionalNestedTrueAlternate,
);
const conditionalNestedTrueFirst = findConditionalName(
  "FIRST", conditionalNestedTrueOther,
);
const conditionalNestedTrueSecond = findConditionalName(
  "SECOND", conditionalNestedTrueFirst,
);
const conditionalNestedTrueThird = findConditionalName(
  "THIRD", conditionalNestedTrueSecond,
);
const conditionalNestedFalse = findConditionalName(
  "local += alternate ? FIRST : other ? SECOND : THIRD",
  conditionalNestedTrueThird,
);
const conditionalNestedFalseAlternate = findConditionalName(
  "alternate", conditionalNestedFalse,
);
const conditionalNestedFalseFirst = findConditionalName(
  "FIRST", conditionalNestedFalseAlternate,
);
const conditionalNestedFalseOther = findConditionalName(
  "other", conditionalNestedFalseFirst,
);
const conditionalNestedFalseSecond = findConditionalName(
  "SECOND", conditionalNestedFalseOther,
);
const conditionalNestedFalseThird = findConditionalName(
  "THIRD", conditionalNestedFalseSecond,
);
const conditionalReturn = findConditionalName(
  "return alternate ? other ? FIRST : SECOND", conditionalNestedFalseThird,
);
const conditionalReturnAlternate = findConditionalName(
  "alternate", conditionalReturn,
);
const conditionalReturnOther = findConditionalName(
  "other", conditionalReturnAlternate,
);
const conditionalReturnFirst = findConditionalName(
  "FIRST", conditionalReturnOther,
);
const conditionalReturnSecond = findConditionalName(
  "SECOND", conditionalReturnFirst,
);
const conditionalReturnLocal = findConditionalName(
  "local ?", conditionalReturnSecond,
);
const conditionalReturnMacro = findConditionalName(
  "CHOICE_MACRO", conditionalReturnLocal,
);
const conditionalReturnThird = findConditionalName(
  "THIRD", conditionalReturnMacro,
);

const conditionalHoverCases = [
  { name: "alternate", kind: "parameter", index: conditionalDirectAlternate,
    allPositions: true },
  { name: "FIRST", kind: "enumConstant", value: "1",
    index: conditionalDirectFirst, allPositions: true },
  { name: "SECOND", kind: "enumConstant", value: "2",
    index: conditionalDirectSecond, allPositions: true },
  { name: "other", kind: "parameter", index: conditionalDirectOther },
  { name: "alternate", kind: "parameter",
    index: conditionalParenthesizedAlternate },
  { name: "alternate", kind: "parameter",
    index: conditionalSubscriptAlternate },
  { name: "alternate", kind: "parameter", index: conditionalCommaAlternate },
  { name: "local", kind: "object", index: conditionalCommaLocal },
  { name: "FIRST", kind: "enumConstant", value: "1",
    index: conditionalCommaFirst },
  { name: "SECOND", kind: "enumConstant", value: "2",
    index: conditionalCommaSecond },
  { name: "local", kind: "object", index: conditionalCommaFalseLocal },
  { name: "THIRD", kind: "enumConstant", value: "3",
    index: conditionalCommaThird },
  { name: "alternate", kind: "parameter",
    index: conditionalNestedTrueAlternate },
  { name: "other", kind: "parameter", index: conditionalNestedTrueOther },
  { name: "FIRST", kind: "enumConstant", value: "1",
    index: conditionalNestedTrueFirst },
  { name: "SECOND", kind: "enumConstant", value: "2",
    index: conditionalNestedTrueSecond },
  { name: "THIRD", kind: "enumConstant", value: "3",
    index: conditionalNestedTrueThird },
  { name: "alternate", kind: "parameter",
    index: conditionalNestedFalseAlternate },
  { name: "FIRST", kind: "enumConstant", value: "1",
    index: conditionalNestedFalseFirst },
  { name: "other", kind: "parameter", index: conditionalNestedFalseOther },
  { name: "SECOND", kind: "enumConstant", value: "2",
    index: conditionalNestedFalseSecond },
  { name: "THIRD", kind: "enumConstant", value: "3",
    index: conditionalNestedFalseThird },
  { name: "alternate", kind: "parameter", index: conditionalReturnAlternate },
  { name: "other", kind: "parameter", index: conditionalReturnOther },
  { name: "FIRST", kind: "enumConstant", value: "1",
    index: conditionalReturnFirst },
  { name: "SECOND", kind: "enumConstant", value: "2",
    index: conditionalReturnSecond },
  { name: "local", kind: "object", index: conditionalReturnLocal },
  { name: "CHOICE_MACRO", kind: "macro", replacement: "7",
    index: conditionalReturnMacro },
  { name: "THIRD", kind: "enumConstant", value: "3",
    index: conditionalReturnThird },
];
const conditionalDeclarations = new Map();
const nativeConditionalSnapshots = new Map();

function conditionalByteOffset(analysisCase, delta) {
  return Buffer.byteLength(
    conditionalSource.source.slice(0, analysisCase.index),
  ) + delta;
}

function assertConditionalHover(result, analysisCase, lifecycle) {
  const hover = result.hover;
  if (result.partial || result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== analysisCase.kind ||
      (["parameter", "object"].includes(analysisCase.kind) &&
       hover.type !== "int") ||
      (analysisCase.kind === "enumConstant" &&
       hover.initializer.constantValue !== analysisCase.value) ||
      (analysisCase.kind === "macro" &&
       hover.macro?.replacement !== analysisCase.replacement)) {
    throw new Error(
      `${lifecycle} conditional hover failed: ${JSON.stringify(result)}`,
    );
  }
  const declaration = conditionalDeclarations.get(analysisCase.name);
  if (declaration) {
    assert.deepStrictEqual(
      hover.declaration,
      declaration,
      `${lifecycle} ${analysisCase.name} declaration range differs`,
    );
  } else {
    conditionalDeclarations.set(analysisCase.name, hover.declaration);
  }
}

for (const analysisCase of conditionalHoverCases) {
  const deltas = analysisCase.allPositions
    ? [0, Math.floor(analysisCase.name.length / 2),
      Buffer.byteLength(analysisCase.name)]
    : [Math.floor(analysisCase.name.length / 2)];
  for (const delta of deltas) {
    const byteOffset = conditionalByteOffset(analysisCase, delta);
    const wasmResult = compiler.analyzeSource(conditionalSource, {
      cursor: { sourceName: conditionalSource.name, byteOffset },
    });
    assertConditionalHover(wasmResult, analysisCase, "reused instance");
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--conditional-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm conditional snapshots differ at byte ${byteOffset}`,
    );
    nativeConditionalSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of [
  conditionalHoverCases[0],
  conditionalHoverCases[1],
  conditionalHoverCases[2],
  conditionalHoverCases[14],
  conditionalHoverCases[20],
  conditionalHoverCases[27],
  conditionalHoverCases[28],
]) {
  const delta = Math.floor(analysisCase.name.length / 2);
  const byteOffset = conditionalByteOffset(analysisCase, delta);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(conditionalSource, {
      cursor: { sourceName: conditionalSource.name, byteOffset },
    });
    assertConditionalHover(freshResult, analysisCase, "fresh instance");
    assert.deepStrictEqual(
      freshResult,
      nativeConditionalSnapshots.get(byteOffset),
      `fresh native/Wasm conditional snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

const genericSource = {
  name: "generic.c",
  source: "#define GENERIC_MACRO 9\n" +
    "typedef int GenericScore;\n" +
    "#define GenericScore() long\n" +
    "struct GenericPlayer { int score; };\n" +
    "union GenericPayload { int score; };\n" +
    "enum GenericState { GENERIC_IDLE = 0 };\n" +
    "#define GenericState() int\n" +
    "enum { GENERIC_MODE = 3 };\n" +
    "#define GENERIC_MODE() 4\n" +
    "int generic_value;\n" +
    "#define generic_value() 5\n" +
    "#define GENERIC_CALL() 6\n" +
    "int generic_identity(int value) { return value; }\n" +
    "int generic_score(struct GenericPlayer value) { return value.score; }\n" +
    "int generic_pointer_score(const struct GenericPlayer *value) { return value ? value->score : 0; }\n" +
    "int generic_pointer_present(const void *value) { return value != 0; }\n" +
    "int generic_array_pointer_present(struct GenericPlayer (*value)[2]) { return value != 0; }\n" +
    "int generic_factory_present(struct GenericPlayer (*value)(void)) { return value != 0; }\n" +
    "const struct GenericPlayer *generic_player_pointer;\n" +
    "struct GenericPlayer (*generic_player_array_pointer)[2];\n" +
    "int main(void) {\n" +
    "  int result = _Generic(generic_value, int: 1, default: 0);\n" +
    "  result += _Generic(generic_identity(generic_value), int: 2, default: 0);\n" +
    "  result += _Generic(GENERIC_MODE, int: 3, default: 0);\n" +
    "  result += _Generic(GENERIC_MACRO, int: 4, default: 0);\n" +
    "  result += _Generic(GENERIC_CALL(), int: 6, default: 0);\n" +
    "  result += _Generic(generic_value, GenericScore: 5, default: 0);\n" +
    "  result += (int)sizeof(_Atomic /* type */ (GenericScore));\n" +
    "  result += (int)_Alignof /* query */ (const GenericScore *);\n" +
    "  result += (int)_Alignof(int [1 + GENERIC_MODE]);\n" +
    "  result += (int)sizeof /* query */ (const struct /* tag */ GenericPlayer);\n" +
    "  result += (int)_Alignof(union /* tag */ GenericPayload);\n" +
    "  result += (int)sizeof(enum /* tag */ GenericState);\n" +
    "  result += generic_score((struct /* literal */ GenericPlayer){ 7 });\n" +
    "  result += (int)(enum /* cast */ GenericState)GENERIC_IDLE;\n" +
    "  result += generic_pointer_score((const struct /* pointer cast */ GenericPlayer * const)0);\n" +
    "  result += generic_pointer_present((const struct /* pointer chain */ GenericPlayer * /* inner */ const * restrict)0);\n" +
    "  result += _Generic((struct GenericPlayer){ 0 }, struct /* association */ GenericPlayer: 8, default: 0);\n" +
    "  result += _Generic(generic_player_pointer, struct GenericPlayer: 0, const struct /* pointer association */ GenericPlayer * const: 9, default: 0);\n" +
    "  result += _Generic(generic_player_pointer, default: 0, const struct /* default first association */ GenericPlayer *: 11);\n" +
    "  result += _Generic(generic_player_array_pointer, struct GenericPlayer *: 0, struct /* array pointer association */ GenericPlayer (*)[2]: 10, default: 0);\n" +
    "  result += generic_array_pointer_present((struct /* array pointer cast */ GenericPlayer (*)[2])0);\n" +
    "  result += generic_array_pointer_present((struct /* quoted array bound */ GenericPlayer (*)[sizeof(\")\")])0);\n" +
    "  result += generic_factory_present((struct /* function pointer cast */ GenericPlayer (*)(void))0);\n" +
    "  result += generic_array_pointer_present(&(struct /* array literal */ GenericPlayer [2]){{ 1 }, { 2 }});\n" +
    "  return result + _Generic(1, int: generic_value, default: 0);\n" +
    "}\n",
};

function findGenericName(name, from = 0) {
  const index = genericSource.source.indexOf(name, from);
  assert.notEqual(index, -1, `missing generic hover anchor: ${name}`);
  return index;
}

const genericMacroDeclaration = findGenericName("GENERIC_MACRO");
const genericCallMacroDeclaration = findGenericName("GENERIC_CALL");
const genericTypedefDeclaration = findGenericName("GenericScore");
const genericStructDeclaration = findGenericName("GenericPlayer");
const genericUnionDeclaration = findGenericName("GenericPayload");
const genericEnumTagDeclaration = findGenericName("GenericState");
const genericEnumDeclaration = findGenericName("GENERIC_MODE");
const genericObjectDeclaration = findGenericName("generic_value");
const genericFunctionDeclaration = findGenericName("generic_identity");
const genericFirstControl = findGenericName(
  "_Generic(generic_value", genericFunctionDeclaration,
);
const genericFirstObjectUse = findGenericName(
  "generic_value", genericFirstControl,
);
const genericCallControl = findGenericName(
  "_Generic(generic_identity", genericFirstObjectUse,
);
const genericFunctionUse = findGenericName(
  "generic_identity", genericCallControl,
);
const genericArgumentUse = findGenericName(
  "generic_value", genericFunctionUse + "generic_identity".length,
);
const genericEnumControl = findGenericName(
  "_Generic(GENERIC_MODE", genericArgumentUse,
);
const genericEnumUse = findGenericName("GENERIC_MODE", genericEnumControl);
const genericMacroControl = findGenericName(
  "_Generic(GENERIC_MACRO", genericEnumUse,
);
const genericMacroUse = findGenericName("GENERIC_MACRO", genericMacroControl);
const genericCallMacroControl = findGenericName(
  "_Generic(GENERIC_CALL()", genericMacroUse,
);
const genericCallMacroUse = findGenericName(
  "GENERIC_CALL", genericCallMacroControl,
);
const genericTypedefControl = findGenericName(
  "_Generic(generic_value, GenericScore", genericCallMacroUse,
);
const genericTypedefUse = findGenericName(
  "GenericScore", genericTypedefControl,
);
const genericAtomicQuery = findGenericName(
  "_Atomic /* type */ (GenericScore",
  genericTypedefUse + "GenericScore".length,
);
const genericAtomicTypedefUse = findGenericName(
  "GenericScore", genericAtomicQuery,
);
const genericAlignof = findGenericName(
  "_Alignof /* query */ (const GenericScore",
  genericAtomicTypedefUse + "GenericScore".length,
);
const genericAlignofTypedefUse = findGenericName(
  "GenericScore", genericAlignof,
);
const genericAlignofBound = findGenericName(
  "_Alignof(int [1 + GENERIC_MODE", genericAlignofTypedefUse,
);
const genericAlignofBoundUse = findGenericName(
  "GENERIC_MODE", genericAlignofBound,
);
const genericStructQuery = findGenericName(
  "sizeof /* query */ (const struct /* tag */ GenericPlayer",
  genericAlignofBoundUse,
);
const genericStructUse = findGenericName("GenericPlayer", genericStructQuery);
const genericUnionQuery = findGenericName(
  "_Alignof(union /* tag */ GenericPayload", genericStructUse,
);
const genericUnionUse = findGenericName("GenericPayload", genericUnionQuery);
const genericEnumTagQuery = findGenericName(
  "sizeof(enum /* tag */ GenericState", genericUnionUse,
);
const genericEnumTagUse = findGenericName(
  "GenericState", genericEnumTagQuery,
);
const genericStructLiteral = findGenericName(
  "generic_score((struct /* literal */ GenericPlayer)", genericEnumTagUse,
);
const genericStructLiteralUse = findGenericName(
  "GenericPlayer", genericStructLiteral,
);
const genericEnumCast = findGenericName(
  "(enum /* cast */ GenericState)", genericStructLiteralUse,
);
const genericEnumCastUse = findGenericName(
  "GenericState", genericEnumCast,
);
const genericPointerCast = findGenericName(
  "(const struct /* pointer cast */ GenericPlayer * const)0",
  genericEnumCastUse,
);
const genericPointerCastUse = findGenericName(
  "GenericPlayer", genericPointerCast,
);
const genericPointerChain = findGenericName(
  "(const struct /* pointer chain */ GenericPlayer * /* inner */ const * restrict)0",
  genericPointerCastUse,
);
const genericPointerChainUse = findGenericName(
  "GenericPlayer", genericPointerChain,
);
const genericTagAssociation = findGenericName(
  "struct /* association */ GenericPlayer", genericPointerChainUse,
);
const genericTagAssociationUse = findGenericName(
  "GenericPlayer", genericTagAssociation,
);
const genericPointerAssociation = findGenericName(
  "struct /* pointer association */ GenericPlayer * const",
  genericTagAssociationUse,
);
const genericPointerAssociationUse = findGenericName(
  "GenericPlayer", genericPointerAssociation,
);
const genericDefaultFirstAssociation = findGenericName(
  "struct /* default first association */ GenericPlayer *",
  genericPointerAssociationUse,
);
const genericDefaultFirstAssociationUse = findGenericName(
  "GenericPlayer", genericDefaultFirstAssociation,
);
const genericArrayPointerAssociation = findGenericName(
  "struct /* array pointer association */ GenericPlayer (*)[2]",
  genericDefaultFirstAssociationUse,
);
const genericArrayPointerAssociationUse = findGenericName(
  "GenericPlayer", genericArrayPointerAssociation,
);
const genericArrayPointerCast = findGenericName(
  "struct /* array pointer cast */ GenericPlayer (*)[2]",
  genericArrayPointerAssociationUse,
);
const genericArrayPointerCastUse = findGenericName(
  "GenericPlayer", genericArrayPointerCast,
);
const genericQuotedArrayBound = findGenericName(
  "struct /* quoted array bound */ GenericPlayer (*)[sizeof(\")\")]",
  genericArrayPointerCastUse,
);
const genericQuotedArrayBoundUse = findGenericName(
  "GenericPlayer", genericQuotedArrayBound,
);
const genericFunctionPointerCast = findGenericName(
  "struct /* function pointer cast */ GenericPlayer (*)(void)",
  genericQuotedArrayBoundUse,
);
const genericFunctionPointerCastUse = findGenericName(
  "GenericPlayer", genericFunctionPointerCast,
);
const genericArrayLiteral = findGenericName(
  "struct /* array literal */ GenericPlayer [2]",
  genericFunctionPointerCastUse,
);
const genericArrayLiteralUse = findGenericName(
  "GenericPlayer", genericArrayLiteral,
);
const genericAssociationValue = findGenericName(
  "int: generic_value", genericArrayLiteralUse,
);
const genericAssociationValueUse = findGenericName(
  "generic_value", genericAssociationValue,
);
const genericHoverCases = [
  { name: "generic_value", kind: "object", index: genericFirstObjectUse },
  { name: "generic_identity", kind: "function", index: genericFunctionUse },
  { name: "generic_value", kind: "object", index: genericArgumentUse },
  { name: "GENERIC_MODE", kind: "enumConstant", value: "3",
    index: genericEnumUse },
  { name: "GENERIC_MACRO", kind: "macro", replacement: "9",
    index: genericMacroUse },
  { name: "GENERIC_CALL", kind: "macro", replacement: "6",
    index: genericCallMacroUse },
  { name: "GenericScore", kind: "typedef", index: genericTypedefUse },
  { name: "GenericScore", kind: "typedef",
    index: genericAtomicTypedefUse },
  { name: "GenericScore", kind: "typedef",
    index: genericAlignofTypedefUse },
  { name: "GENERIC_MODE", kind: "enumConstant", value: "3",
    index: genericAlignofBoundUse },
  { name: "GenericPlayer", kind: "tag", index: genericStructUse },
  { name: "GenericPayload", kind: "tag", index: genericUnionUse },
  { name: "GenericState", kind: "tag", index: genericEnumTagUse },
  { name: "GenericPlayer", kind: "tag", index: genericStructLiteralUse },
  { name: "GenericState", kind: "tag", index: genericEnumCastUse },
  { name: "GenericPlayer", kind: "tag", index: genericPointerCastUse },
  { name: "GenericPlayer", kind: "tag", index: genericPointerChainUse },
  { name: "GenericPlayer", kind: "tag", index: genericTagAssociationUse },
  { name: "GenericPlayer", kind: "tag",
    index: genericPointerAssociationUse },
  { name: "GenericPlayer", kind: "tag",
    index: genericDefaultFirstAssociationUse },
  { name: "GenericPlayer", kind: "tag",
    index: genericArrayPointerAssociationUse },
  { name: "GenericPlayer", kind: "tag",
    index: genericArrayPointerCastUse },
  { name: "GenericPlayer", kind: "tag",
    index: genericQuotedArrayBoundUse },
  { name: "GenericPlayer", kind: "tag",
    index: genericFunctionPointerCastUse },
  { name: "GenericPlayer", kind: "tag", index: genericArrayLiteralUse },
  { name: "generic_value", kind: "object",
    index: genericAssociationValueUse },
];
const genericDeclarations = new Map([
  ["generic_value", genericObjectDeclaration],
  ["generic_identity", genericFunctionDeclaration],
  ["GENERIC_MODE", genericEnumDeclaration],
  ["GENERIC_MACRO", genericMacroDeclaration],
  ["GENERIC_CALL", genericCallMacroDeclaration],
  ["GenericScore", genericTypedefDeclaration],
  ["GenericPlayer", genericStructDeclaration],
  ["GenericPayload", genericUnionDeclaration],
  ["GenericState", genericEnumTagDeclaration],
]);
const nativeGenericSnapshots = new Map();

function genericByteOffset(analysisCase, delta) {
  return Buffer.byteLength(genericSource.source.slice(0, analysisCase.index)) +
    delta;
}

function assertGenericHover(result, analysisCase, lifecycle) {
  const hover = result.hover;
  const declarationStart = genericDeclarations.get(analysisCase.name);
  if (result.partial || result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== analysisCase.kind ||
      hover.declaration.sourceName !== genericSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.declaration.end.offset !==
        declarationStart + Buffer.byteLength(analysisCase.name) ||
      (analysisCase.kind === "enumConstant" &&
       hover.initializer.constantValue !== analysisCase.value) ||
      (analysisCase.kind === "macro" &&
       hover.macro?.replacement !== analysisCase.replacement)) {
    throw new Error(
      `${lifecycle} generic hover failed: ${JSON.stringify(result)}`,
    );
  }
}

for (const analysisCase of genericHoverCases) {
  for (const delta of [
    0,
    Math.floor(analysisCase.name.length / 2),
    Buffer.byteLength(analysisCase.name),
  ]) {
    const byteOffset = genericByteOffset(analysisCase, delta);
    const wasmResult = compiler.analyzeSource(genericSource, {
      cursor: { sourceName: genericSource.name, byteOffset },
    });
    assertGenericHover(wasmResult, analysisCase, "reused instance");
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--generic-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm generic snapshots differ at byte ${byteOffset}`,
    );
    nativeGenericSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of genericHoverCases) {
  const delta = Math.floor(analysisCase.name.length / 2);
  const byteOffset = genericByteOffset(analysisCase, delta);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(genericSource, {
      cursor: { sourceName: genericSource.name, byteOffset },
    });
    assertGenericHover(freshResult, analysisCase, "fresh instance");
    assert.deepStrictEqual(
      freshResult,
      nativeGenericSnapshots.get(byteOffset),
      `fresh native/Wasm generic snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

const functionDeclaratorSource = {
  name: "function-declarator.c",
  source: "int increment(int value) { return value + 1; }\n" +
    "int old_sum(left, right) /* ; { comment } */\n" +
    "int left, right;\n" +
    "{ return left + right; }\n" +
    "int old_apply(callback, value)\n" +
    "int callback(int);\n" +
    "register int value;\n" +
    "{ return callback(value); }\n" +
    "int old_member(value)\n" +
    "struct LocalValue { int member; } value;\n" +
    "{ return value.member; }\n" +
    "int old_array(values)\n" +
    "int values[';'];\n" +
    "{ return values[0]; }\n" +
    "int first(void), second(void);\n" +
    "int third(void), brace_values[2] = { 1, 2 };\n" +
    "typedef int Scalar;\n" +
    "struct FileRecord { const int *member; unsigned bits : 3; int values[4]; int (*callback_member)(int); };\n" +
    "union FileUnion { long member; };\n" +
    "int takes_scalar(Scalar);\n" +
    "int parameter_prototype(int named, const int *pointer, int proto_callback(int));\n" +
    "int tagged_parameter_prototype(struct /* scope */ PrototypeRecord { int member; } value);\n" +
    "int nested_tag_parameter_prototype(int callback(union /* scope */ NestedPayload { int member; } value));\n" +
    "int enum_parameter_prototype(enum /* scope */ PrototypeState { PROTOTYPE_READY = 3, PROTOTYPE_BUSY } value);\n" +
    "int nested_enum_parameter_prototype(int callback(enum /* scope */ NestedState { NESTED_READY = 7 } value));\n" +
    "int unicode_parameter(int 値) { return 値; }\n" +
    "int (parenthesized)(int value) { return value + 1; }\n" +
    "int target(int value);\n" +
    "int (*(factory(void)))(int) { return target; }\n" +
    "int (*(seeded_factory(int seed)))(int) { return target; }\n" +
    "#define active_member_macro() 9\n" +
    "struct MacroRecord { int active_member_macro; int future_member_macro; };\n" +
    "int read_macro_member(struct MacroRecord value) { return value.active_member_macro; }\n" +
    "#define future_member_macro 7\n" +
    "int read_file_members(struct FileRecord *record, union FileUnion value) {\n" +
    "  return (record->member != 0) + (int)value.member;\n" +
    "}\n" +
    "int forward_label(int value) {\n" +
    "  {\n" +
    "    int shared_label = value;\n" +
    "    goto /* forward */ shared_label;\n" +
    "  }\n" +
    "shared_label:\n" +
    "  return value;\n" +
    "}\n" +
    "int backward_label(int value) {\n" +
    "shared_label:\n" +
    "  {\n" +
    "    int shared_label = value;\n" +
    "    if (value) goto /* backward */ shared_label;\n" +
    "    return shared_label;\n" +
    "  }\n" +
    "}\n",
};
const functionDeclaratorCases = [
  { name: "old_sum", returnType: "int", parameterCount: 2, hasPrototype: false,
    hasDefinition: true },
  { name: "old_apply", returnType: "int", parameterCount: 2, hasPrototype: false,
    hasDefinition: true },
  { name: "old_member", returnType: "int", parameterCount: 1, hasPrototype: false,
    hasDefinition: true },
  { name: "old_array", returnType: "int", parameterCount: 1, hasPrototype: false,
    hasDefinition: true },
  { name: "first", returnType: "int", parameterCount: 0, hasPrototype: true,
    hasDefinition: false },
  { name: "second", returnType: "int", parameterCount: 0, hasPrototype: true,
    hasDefinition: false },
  { name: "third", returnType: "int", parameterCount: 0, hasPrototype: true,
    hasDefinition: false },
  { name: "takes_scalar", returnType: "int", parameterCount: 1, hasPrototype: true,
    hasDefinition: false },
  { name: "parameter_prototype", returnType: "int", parameterCount: 3,
    hasPrototype: true, hasDefinition: false },
  { name: "unicode_parameter", returnType: "int", parameterCount: 1,
    hasPrototype: true, hasDefinition: true },
  { name: "parenthesized", returnType: "int", parameterCount: 1,
    hasPrototype: true, hasDefinition: true },
  { name: "factory", returnType: "int (*)(int)", parameterCount: 0,
    hasPrototype: true, hasDefinition: true },
  { name: "seeded_factory", returnType: "int (*)(int)", parameterCount: 1,
    hasPrototype: true, hasDefinition: true },
];
const nativeFunctionDeclaratorSnapshots = new Map();
const functionParameterCases = [
  { anchor: "increment(int value", name: "value", type: "int",
    declarationAnchor: "increment(int value" },
  { anchor: "old_sum(left", name: "left", type: "int",
    declarationAnchor: "int left, right;" },
  { anchor: "int callback(int);", name: "callback", type: "int (*)(int)",
    declarationAnchor: "int callback(int);" },
  { anchor: "register int value;", name: "value", type: "int",
    declarationAnchor: "register int value;" },
  { anchor: "old_member(value)", name: "value", type: "struct LocalValue",
    declarationAnchor: "} value;" },
  { anchor: "old_array(values)", name: "values", type: "int *",
    declarationAnchor: "int values[';'];" },
  { anchor: "*pointer,", name: "pointer", type: "const int *",
    declarationAnchor: "*pointer," },
  { anchor: "proto_callback(int)", name: "proto_callback",
    type: "int (*)(int)", declarationAnchor: "proto_callback(int)" },
  { anchor: "unicode_parameter(int 値", name: "値", type: "int",
    declarationAnchor: "unicode_parameter(int 値" },
  { anchor: "parenthesized)(int value", name: "value", type: "int",
    declarationAnchor: "parenthesized)(int value" },
  { anchor: "(int seed))", name: "seed", type: "int",
    declarationAnchor: "(int seed))" },
];
const nativeFunctionParameterSnapshots = new Map();
const functionParameterTagCases = [
  { name: "PrototypeRecord", type: "struct PrototypeRecord", scopeDepth: 1 },
  { name: "NestedPayload", type: "union NestedPayload", scopeDepth: 2 },
  { name: "PrototypeState", type: "enum PrototypeState", scopeDepth: 1 },
  { name: "NestedState", type: "enum NestedState", scopeDepth: 2 },
];
const nativeFunctionParameterTagSnapshots = new Map();
const aggregateMemberCases = [
  { anchor: "FileRecord { const int *member;", name: "member",
    type: "const int *", scopeDepth: 1 },
  { anchor: "unsigned bits : 3;", name: "bits",
    type: "unsigned int", scopeDepth: 1 },
  { anchor: "int values[4];", name: "values",
    type: "int [4]", scopeDepth: 1 },
  { anchor: "(*callback_member)(int);", name: "callback_member",
    type: "int (*)(int)", scopeDepth: 1 },
  { anchor: "FileUnion { long member;", name: "member",
    type: "long", scopeDepth: 1 },
  { anchor: "PrototypeRecord { int member; }", name: "member",
    type: "int", scopeDepth: 2 },
  { anchor: "NestedPayload { int member; }", name: "member",
    type: "int", scopeDepth: 3 },
  { anchor: "MacroRecord { int active_member_macro;",
    name: "active_member_macro", type: "int", scopeDepth: 1 },
  { anchor: "int future_member_macro;", name: "future_member_macro",
    type: "int", scopeDepth: 1 },
];
const nativeAggregateMemberSnapshots = new Map();
const aggregateMemberUseCases = [
  { anchor: "return value.member;", name: "member", type: "int",
    declarationAnchor: "LocalValue { int member; }" },
  { anchor: "record->member", name: "member", type: "const int *",
    declarationAnchor: "FileRecord { const int *member;" },
  { anchor: "(int)value.member;", name: "member", type: "long",
    declarationAnchor: "FileUnion { long member; }" },
  { anchor: "return value.active_member_macro;", name: "active_member_macro",
    type: "int", declarationAnchor: "MacroRecord { int active_member_macro;" },
];
const nativeAggregateMemberUseSnapshots = new Map();
const labelHoverCases = [
  { anchor: "goto /* forward */ shared_label;", name: "shared_label",
    declarationAnchor: "shared_label:\n  return value;", objectVisible: true },
  { anchor: "shared_label:\n  return value;", name: "shared_label",
    declarationAnchor: "shared_label:\n  return value;", objectVisible: false },
  { anchor: "int backward_label(int value) {\nshared_label:",
    name: "shared_label",
    declarationAnchor: "int backward_label(int value) {\nshared_label:",
    objectVisible: false },
  { anchor: "goto /* backward */ shared_label;", name: "shared_label",
    declarationAnchor: "int backward_label(int value) {\nshared_label:",
    objectVisible: true },
];
const nativeLabelHoverSnapshots = new Map();
const functionParameterEnumCases = [
  { name: "PROTOTYPE_READY", value: "3", scopeDepth: 1 },
  { name: "PROTOTYPE_BUSY", value: "4", scopeDepth: 1 },
  { name: "NESTED_READY", value: "7", scopeDepth: 2 },
];
const nativeFunctionParameterEnumSnapshots = new Map();

function functionDeclaratorByteOffset(stringIndex) {
  return stringIndex < 0
    ? -1
    : Buffer.byteLength(functionDeclaratorSource.source.slice(0, stringIndex));
}

function assertFunctionDeclaratorHover(result, analysisCase, lifecycle) {
  const declarationStart = functionDeclaratorByteOffset(
    functionDeclaratorSource.source.indexOf(analysisCase.name),
  );
  const hover = result.hover;
  if (declarationStart < 0 || result.partial ||
      result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== "function" ||
      hover.declaration.sourceName !== functionDeclaratorSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.function?.returnType !== analysisCase.returnType ||
      hover.function.hasPrototype !== analysisCase.hasPrototype ||
      hover.function.parameters.length !== analysisCase.parameterCount ||
      (analysisCase.hasDefinition
        ? hover.definition?.start.offset !== declarationStart
        : hover.definition !== null)) {
    throw new Error(
      `${lifecycle} function declarator hover failed: ${JSON.stringify(result)}`,
    );
  }
}

function functionParameterOffsets(analysisCase) {
  const anchorStart = functionDeclaratorSource.source.indexOf(
    analysisCase.anchor,
  );
  const nameStart = functionDeclaratorSource.source.indexOf(
    analysisCase.name, anchorStart,
  );
  const declarationAnchorStart = functionDeclaratorSource.source.indexOf(
    analysisCase.declarationAnchor,
  );
  const declarationStart = functionDeclaratorSource.source.indexOf(
    analysisCase.name, declarationAnchorStart,
  );
  return {
    anchorStart,
    nameStart: functionDeclaratorByteOffset(nameStart),
    declarationStart: functionDeclaratorByteOffset(declarationStart),
  };
}

function assertFunctionParameterHover(result, analysisCase, lifecycle) {
  const { anchorStart, nameStart, declarationStart } =
    functionParameterOffsets(analysisCase);
  const hover = result.hover;
  if (anchorStart < 0 || nameStart < 0 || declarationStart < 0 ||
      result.partial || result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== "parameter" ||
      hover.type !== analysisCase.type || hover.signature !== "" ||
      hover.scopeDepth !== 1 || hover.definition !== null ||
      hover.declaration.sourceName !== functionDeclaratorSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.declaration.end.offset !==
        declarationStart + Buffer.byteLength(analysisCase.name)) {
    throw new Error(
      `${lifecycle} function parameter hover failed: ${JSON.stringify(result)}`,
    );
  }
}

function functionScopedDeclarationStart(analysisCase) {
  const anchorStart = analysisCase.anchor
    ? functionDeclaratorSource.source.indexOf(analysisCase.anchor)
    : 0;
  if (anchorStart < 0) return -1;
  return functionDeclaratorByteOffset(
    functionDeclaratorSource.source.indexOf(analysisCase.name, anchorStart),
  );
}

function assertFunctionParameterTagHover(result, analysisCase, lifecycle) {
  const declarationStart = functionScopedDeclarationStart(analysisCase);
  const hover = result.hover;
  if (declarationStart < 0 || result.partial ||
      result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== "tag" ||
      hover.nameSpace !== "tag" || hover.type !== analysisCase.type ||
      hover.signature !== "" || hover.storageClass !== "" ||
      hover.scopeDepth !== analysisCase.scopeDepth || hover.definition !== null ||
      hover.declaration.sourceName !== functionDeclaratorSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.declaration.end.offset !==
        declarationStart + Buffer.byteLength(analysisCase.name)) {
    throw new Error(
      `${lifecycle} function prototype-scope tag hover failed: ${JSON.stringify(result)}`,
    );
  }
}

function assertFunctionParameterEnumHover(result, analysisCase, lifecycle) {
  const declarationStart = functionScopedDeclarationStart(analysisCase);
  const hover = result.hover;
  if (declarationStart < 0 || result.partial ||
      result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== "enumConstant" ||
      hover.nameSpace !== "ordinary" || hover.type !== "int" ||
      hover.signature !== "" || hover.storageClass !== "" ||
      hover.scopeDepth !== analysisCase.scopeDepth || hover.definition !== null ||
      hover.initializer?.state !== "explicitConstant" ||
      hover.initializer.constantValue !== analysisCase.value ||
      hover.declaration.sourceName !== functionDeclaratorSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.declaration.end.offset !==
        declarationStart + Buffer.byteLength(analysisCase.name)) {
    throw new Error(
      `${lifecycle} function prototype-scope enum hover failed: ${JSON.stringify(result)}`,
    );
  }
}

function assertAggregateMemberHover(result, analysisCase, lifecycle) {
  const declarationStart = functionScopedDeclarationStart(analysisCase);
  const hover = result.hover;
  if (declarationStart < 0 || result.partial ||
      result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== "member" ||
      hover.nameSpace !== "member" || hover.type !== analysisCase.type ||
      hover.signature !== "" || hover.storageClass !== "member" ||
      hover.scopeDepth !== analysisCase.scopeDepth || hover.definition !== null ||
      hover.initializer?.state !== "none" ||
      hover.declaration.sourceName !== functionDeclaratorSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.declaration.end.offset !==
        declarationStart + Buffer.byteLength(analysisCase.name)) {
    throw new Error(
      `${lifecycle} aggregate member hover failed: ${JSON.stringify(result)}`,
    );
  }
}

function aggregateMemberUseOffsets(analysisCase) {
  const anchorStart = functionDeclaratorSource.source.indexOf(
    analysisCase.anchor,
  );
  const nameStart = functionDeclaratorSource.source.indexOf(
    analysisCase.name, anchorStart,
  );
  const declarationAnchorStart = functionDeclaratorSource.source.indexOf(
    analysisCase.declarationAnchor,
  );
  const declarationStart = functionDeclaratorSource.source.indexOf(
    analysisCase.name, declarationAnchorStart,
  );
  return {
    anchorStart,
    nameStart: functionDeclaratorByteOffset(nameStart),
    declarationStart: functionDeclaratorByteOffset(declarationStart),
  };
}

function assertAggregateMemberUseHover(result, analysisCase, lifecycle) {
  const { anchorStart, nameStart, declarationStart } =
    aggregateMemberUseOffsets(analysisCase);
  const hover = result.hover;
  if (anchorStart < 0 || nameStart < 0 || declarationStart < 0 ||
      result.partial || result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== "member" ||
      hover.nameSpace !== "member" || hover.type !== analysisCase.type ||
      hover.signature !== "" || hover.storageClass !== "member" ||
      hover.scopeDepth !== 2 || hover.definition !== null ||
      hover.initializer?.state !== "none" ||
      hover.declaration.sourceName !== functionDeclaratorSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.declaration.end.offset !==
        declarationStart + Buffer.byteLength(analysisCase.name)) {
    throw new Error(
      `${lifecycle} aggregate member use hover failed: ${JSON.stringify(result)}`,
    );
  }
}

function labelHoverOffsets(analysisCase) {
  const anchorStart = functionDeclaratorSource.source.indexOf(
    analysisCase.anchor,
  );
  const nameStart = functionDeclaratorSource.source.indexOf(
    analysisCase.name, anchorStart,
  );
  const declarationAnchorStart = functionDeclaratorSource.source.indexOf(
    analysisCase.declarationAnchor,
  );
  const declarationStart = functionDeclaratorSource.source.indexOf(
    analysisCase.name, declarationAnchorStart,
  );
  return {
    anchorStart,
    nameStart: functionDeclaratorByteOffset(nameStart),
    declarationStart: functionDeclaratorByteOffset(declarationStart),
  };
}

function assertLabelHover(result, analysisCase, lifecycle) {
  const { anchorStart, nameStart, declarationStart } =
    labelHoverOffsets(analysisCase);
  const hover = result.hover;
  const sameNamedObjects = result.completionItems.filter(
    (item) => item.name === analysisCase.name && item.kind === "object",
  );
  const labels = result.completionItems.filter(
    (item) => item.kind === "label",
  );
  if (anchorStart < 0 || nameStart < 0 || declarationStart < 0 ||
      result.partial || result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== "label" ||
      hover.nameSpace !== "label" || hover.type !== "" ||
      hover.signature !== `${analysisCase.name}:` ||
      hover.storageClass !== "" || hover.scopeDepth !== 1 ||
      hover.definition !== null || hover.initializer?.state !== "none" ||
      hover.declaration.sourceName !== functionDeclaratorSource.name ||
      hover.declaration.start.offset !== declarationStart ||
      hover.declaration.end.offset !==
        declarationStart + Buffer.byteLength(analysisCase.name) ||
      labels.length !== 1 || labels[0].name !== hover.name ||
      sameNamedObjects.length !== (analysisCase.objectVisible ? 1 : 0)) {
    throw new Error(
      `${lifecycle} label hover failed: ${JSON.stringify(result)}`,
    );
  }
}

for (const analysisCase of functionDeclaratorCases) {
  const declarationStart = functionDeclaratorByteOffset(
    functionDeclaratorSource.source.indexOf(analysisCase.name),
  );
  for (const delta of [
    0,
    Math.floor(analysisCase.name.length / 2),
    Buffer.byteLength(analysisCase.name),
  ]) {
    const byteOffset = declarationStart + delta;
    const wasmResult = compiler.analyzeSource(functionDeclaratorSource, {
      cursor: { sourceName: functionDeclaratorSource.name, byteOffset },
    });
    assertFunctionDeclaratorHover(
      wasmResult, analysisCase, "reused instance",
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--function-declarator-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm function declarator snapshots differ at byte ${byteOffset}`,
    );
    nativeFunctionDeclaratorSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of functionParameterCases) {
  const { nameStart } = functionParameterOffsets(analysisCase);
  const nameByteLength = Buffer.byteLength(analysisCase.name);
  for (const delta of [
    0,
    Math.floor(nameByteLength / 2),
    nameByteLength,
  ]) {
    const byteOffset = nameStart + delta;
    const wasmResult = compiler.analyzeSource(functionDeclaratorSource, {
      cursor: { sourceName: functionDeclaratorSource.name, byteOffset },
    });
    assertFunctionParameterHover(
      wasmResult, analysisCase, "reused instance",
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--function-declarator-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm function parameter snapshots differ at byte ${byteOffset}`,
    );
    nativeFunctionParameterSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of functionParameterTagCases) {
  const declarationStart = functionScopedDeclarationStart(analysisCase);
  const nameByteLength = Buffer.byteLength(analysisCase.name);
  for (const delta of [0, Math.floor(nameByteLength / 2), nameByteLength]) {
    const byteOffset = declarationStart + delta;
    const wasmResult = compiler.analyzeSource(functionDeclaratorSource, {
      cursor: { sourceName: functionDeclaratorSource.name, byteOffset },
    });
    assertFunctionParameterTagHover(
      wasmResult, analysisCase, "reused instance",
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--function-declarator-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm function prototype-scope tag snapshots differ at byte ${byteOffset}`,
    );
    nativeFunctionParameterTagSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of aggregateMemberCases) {
  const declarationStart = functionScopedDeclarationStart(analysisCase);
  const nameByteLength = Buffer.byteLength(analysisCase.name);
  for (const delta of [0, Math.floor(nameByteLength / 2), nameByteLength]) {
    const byteOffset = declarationStart + delta;
    const wasmResult = compiler.analyzeSource(functionDeclaratorSource, {
      cursor: { sourceName: functionDeclaratorSource.name, byteOffset },
    });
    assertAggregateMemberHover(
      wasmResult, analysisCase, "reused instance",
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--function-declarator-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm aggregate member snapshots differ at byte ${byteOffset}`,
    );
    nativeAggregateMemberSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of aggregateMemberUseCases) {
  const { nameStart } = aggregateMemberUseOffsets(analysisCase);
  const nameByteLength = Buffer.byteLength(analysisCase.name);
  for (const delta of [0, Math.floor(nameByteLength / 2), nameByteLength]) {
    const byteOffset = nameStart + delta;
    const wasmResult = compiler.analyzeSource(functionDeclaratorSource, {
      cursor: { sourceName: functionDeclaratorSource.name, byteOffset },
    });
    assertAggregateMemberUseHover(
      wasmResult, analysisCase, "reused instance",
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--function-declarator-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm aggregate member use snapshots differ at byte ${byteOffset}`,
    );
    nativeAggregateMemberUseSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of labelHoverCases) {
  const { nameStart } = labelHoverOffsets(analysisCase);
  const nameByteLength = Buffer.byteLength(analysisCase.name);
  for (const delta of [0, Math.floor(nameByteLength / 2), nameByteLength]) {
    const byteOffset = nameStart + delta;
    const wasmResult = compiler.analyzeSource(functionDeclaratorSource, {
      cursor: { sourceName: functionDeclaratorSource.name, byteOffset },
    });
    assertLabelHover(wasmResult, analysisCase, "reused instance");
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--function-declarator-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm label snapshots differ at byte ${byteOffset}`,
    );
    nativeLabelHoverSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of functionParameterEnumCases) {
  const declarationStart = functionScopedDeclarationStart(analysisCase);
  const nameByteLength = Buffer.byteLength(analysisCase.name);
  for (const delta of [0, Math.floor(nameByteLength / 2), nameByteLength]) {
    const byteOffset = declarationStart + delta;
    const wasmResult = compiler.analyzeSource(functionDeclaratorSource, {
      cursor: { sourceName: functionDeclaratorSource.name, byteOffset },
    });
    assertFunctionParameterEnumHover(
      wasmResult, analysisCase, "reused instance",
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--function-declarator-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm function prototype-scope enum snapshots differ at byte ${byteOffset}`,
    );
    nativeFunctionParameterEnumSnapshots.set(byteOffset, nativeResult);
  }
}

for (const analysisCase of functionDeclaratorCases) {
  const declarationStart = functionDeclaratorByteOffset(
    functionDeclaratorSource.source.indexOf(analysisCase.name),
  );
  const byteOffset = declarationStart +
    Math.floor(analysisCase.name.length / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDeclaratorSource,
      { cursor: { sourceName: functionDeclaratorSource.name, byteOffset } },
    );
    assertFunctionDeclaratorHover(
      freshResult, analysisCase, "fresh instance",
    );
    assert.deepStrictEqual(
      freshResult,
      nativeFunctionDeclaratorSnapshots.get(byteOffset),
      `fresh native/Wasm function declarator snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

for (const analysisCase of functionParameterCases) {
  const { nameStart } = functionParameterOffsets(analysisCase);
  const byteOffset = nameStart +
    Math.floor(Buffer.byteLength(analysisCase.name) / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDeclaratorSource,
      { cursor: { sourceName: functionDeclaratorSource.name, byteOffset } },
    );
    assertFunctionParameterHover(
      freshResult, analysisCase, "fresh instance",
    );
    assert.deepStrictEqual(
      freshResult,
      nativeFunctionParameterSnapshots.get(byteOffset),
      `fresh native/Wasm function parameter snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

for (const analysisCase of functionParameterTagCases) {
  const byteOffset = functionScopedDeclarationStart(analysisCase) +
    Math.floor(Buffer.byteLength(analysisCase.name) / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDeclaratorSource,
      { cursor: { sourceName: functionDeclaratorSource.name, byteOffset } },
    );
    assertFunctionParameterTagHover(
      freshResult, analysisCase, "fresh instance",
    );
    assert.deepStrictEqual(
      freshResult,
      nativeFunctionParameterTagSnapshots.get(byteOffset),
      `fresh native/Wasm function prototype-scope tag snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

for (const analysisCase of aggregateMemberCases) {
  const byteOffset = functionScopedDeclarationStart(analysisCase) +
    Math.floor(Buffer.byteLength(analysisCase.name) / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDeclaratorSource,
      { cursor: { sourceName: functionDeclaratorSource.name, byteOffset } },
    );
    assertAggregateMemberHover(
      freshResult, analysisCase, "fresh instance",
    );
    assert.deepStrictEqual(
      freshResult,
      nativeAggregateMemberSnapshots.get(byteOffset),
      `fresh native/Wasm aggregate member snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

for (const analysisCase of aggregateMemberUseCases) {
  const { nameStart } = aggregateMemberUseOffsets(analysisCase);
  const byteOffset = nameStart +
    Math.floor(Buffer.byteLength(analysisCase.name) / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDeclaratorSource,
      { cursor: { sourceName: functionDeclaratorSource.name, byteOffset } },
    );
    assertAggregateMemberUseHover(
      freshResult, analysisCase, "fresh instance",
    );
    assert.deepStrictEqual(
      freshResult,
      nativeAggregateMemberUseSnapshots.get(byteOffset),
      `fresh native/Wasm aggregate member use snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

for (const analysisCase of labelHoverCases) {
  const { nameStart } = labelHoverOffsets(analysisCase);
  const byteOffset = nameStart +
    Math.floor(Buffer.byteLength(analysisCase.name) / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDeclaratorSource,
      { cursor: { sourceName: functionDeclaratorSource.name, byteOffset } },
    );
    assertLabelHover(freshResult, analysisCase, "fresh instance");
    assert.deepStrictEqual(
      freshResult,
      nativeLabelHoverSnapshots.get(byteOffset),
      `fresh native/Wasm label snapshot differs at byte ${byteOffset}`,
    );
  } finally {
    freshCompiler.dispose();
  }
}

for (const analysisCase of functionParameterEnumCases) {
  const byteOffset = functionScopedDeclarationStart(analysisCase) +
    Math.floor(Buffer.byteLength(analysisCase.name) / 2);
  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(
      functionDeclaratorSource,
      { cursor: { sourceName: functionDeclaratorSource.name, byteOffset } },
    );
    assertFunctionParameterEnumHover(
      freshResult, analysisCase, "fresh instance",
    );
    assert.deepStrictEqual(
      freshResult,
      nativeFunctionParameterEnumSnapshots.get(byteOffset),
      `fresh native/Wasm function prototype-scope enum snapshot differs at byte ${byteOffset}`,
    );
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
    definition: hover.definition,
    definitionConflict: hover.definitionConflict,
    definitionCandidates: hover.definitionCandidates,
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

const projectHeaders = {
  "player.h":
    "void move_and_draw(void);\n" +
    "void declared_only_project(void);\n" +
    "void local_only(void);\n",
};
const projectPlayerSource = {
  name: "player.c",
  source: "#include \"player.h\"\n/* プレイヤー */\nvoid move_and_draw(void) {}\n",
};
const projectMainSource = {
  name: "main.c",
  source:
    "#include \"player.h\"\n\n" +
    "int main(void) {\n" +
    "  move_and_draw();\n" +
    "  declared_only_project();\n" +
    "  local_only();\n" +
    "  return 0;\n" +
    "}\n",
};
const projectStaticA = {
  name: "static_a.c",
  source: "static void local_only(void) {}\n",
};
const projectStaticB = {
  name: "static_b.c",
  source: "static void local_only(void) {}\n",
};
const projectSources = [
  projectPlayerSource,
  projectMainSource,
  projectStaticA,
  projectStaticB,
];
const projectUseStart = projectMainSource.source.indexOf(moveName);
const projectDefinitionStart = Buffer.byteLength(
  projectPlayerSource.source.slice(
    0, projectPlayerSource.source.indexOf(moveName),
  ),
);
const projectDeclarationStart = projectHeaders["player.h"].indexOf(moveName);
const wasmProjectParity = compiler.analyzeProjectSource(
  projectMainSource,
  {
    projectRevision: 1,
    projectSources,
    headers: projectHeaders,
    cursor: {
      sourceName: projectMainSource.name,
      byteOffset: projectUseStart + Math.floor(moveName.length / 2),
    },
  },
);
const nativeProjectParity = JSON.parse(execFileSync(
  nativeAnalysisPath,
  ["--project-function-parity-json"],
  { encoding: "utf8" },
));
assert.deepStrictEqual(
  wasmProjectParity,
  nativeProjectParity,
  "native and Wasm project function snapshots differ",
);

const projectHoverSources = [
  { source: projectMainSource, offset: projectUseStart },
  {
    source: { name: "player.h", source: projectHeaders["player.h"] },
    offset: projectDeclarationStart,
  },
  { source: projectPlayerSource, offset: projectDefinitionStart },
];
for (const hoverSource of projectHoverSources) {
  for (const cursorDelta of functionCursorDeltas) {
    const result = compiler.analyzeProjectSource(hoverSource.source, {
      projectRevision: 1,
      projectSources,
      headers: projectHeaders,
      cursor: {
        sourceName: hoverSource.source.name,
        byteOffset: hoverSource.offset + cursorDelta,
      },
    });
    if (result.hover?.name !== moveName ||
        result.hover.declaration.sourceName !== "player.h" ||
        result.hover.declaration.start.offset !== projectDeclarationStart ||
        result.hover.definition?.sourceName !== "player.c" ||
        result.hover.definition.start.offset !== projectDefinitionStart ||
        result.hover.definition.end.offset !==
          projectDefinitionStart + Buffer.byteLength(moveName) ||
        result.hover.definitionConflict ||
        result.hover.definitionCandidates.length !== 1) {
      throw new Error(
        `project function hover failed: ${JSON.stringify(result)}`,
      );
    }
  }
}

const declaredOnlyProjectStart = projectMainSource.source.indexOf(
  "declared_only_project",
);
const declaredOnlyProjectResult = compiler.analyzeProjectSource(
  projectMainSource,
  {
    projectRevision: 1,
    projectSources,
    headers: projectHeaders,
    cursor: {
      sourceName: projectMainSource.name,
      byteOffset: declaredOnlyProjectStart + 2,
    },
  },
);
if (declaredOnlyProjectResult.hover?.definition !== null ||
    declaredOnlyProjectResult.hover?.definitionConflict ||
    declaredOnlyProjectResult.hover?.definitionCandidates.length !== 0) {
  throw new Error(
    `declaration-only project function gained a definition: ${JSON.stringify(declaredOnlyProjectResult)}`,
  );
}

const localOnlyStart = projectMainSource.source.indexOf("local_only");
const localOnlyResult = compiler.analyzeProjectSource(projectMainSource, {
  projectRevision: 1,
  projectSources,
  headers: projectHeaders,
  cursor: {
    sourceName: projectMainSource.name,
    byteOffset: localOnlyStart + 2,
  },
});
if (localOnlyResult.hover?.definition !== null ||
    localOnlyResult.hover?.definitionCandidates.length !== 0) {
  throw new Error(
    `translation-unit-local definition leaked into project index: ${JSON.stringify(localOnlyResult)}`,
  );
}

const movedProjectPlayer = {
  name: "player.c",
  source: "#include \"player.h\"\n\n\n\nvoid move_and_draw(void) {}\n",
};
const movedProjectSources = [movedProjectPlayer, ...projectSources.slice(1)];
const movedDefinitionStart = movedProjectPlayer.source.indexOf(moveName);
const movedResult = compiler.analyzeProjectSource(projectMainSource, {
  projectRevision: 2,
  projectSources: movedProjectSources,
  headers: projectHeaders,
  cursor: {
    sourceName: projectMainSource.name,
    byteOffset: projectUseStart + 2,
  },
});
assert.equal(movedResult.hover?.definition?.start.offset, movedDefinitionStart);

const removedProjectPlayer = {
  name: "player.c",
  source: "#include \"player.h\"\n",
};
const removedProjectSources = [removedProjectPlayer, ...projectSources.slice(1)];
const cachedResult = compiler.analyzeProjectSource(projectMainSource, {
  projectRevision: 2,
  projectSources: removedProjectSources,
  headers: projectHeaders,
  cursor: {
    sourceName: projectMainSource.name,
    byteOffset: projectUseStart + 2,
  },
});
assert.equal(cachedResult.hover?.definition?.start.offset, movedDefinitionStart);
const removedResult = compiler.analyzeProjectSource(projectMainSource, {
  projectRevision: 3,
  projectSources: removedProjectSources,
  headers: projectHeaders,
  cursor: {
    sourceName: projectMainSource.name,
    byteOffset: projectUseStart + 2,
  },
});
assert.equal(removedResult.hover?.definition, null);

const duplicateProjectSources = [
  {
    name: "player.c",
    source: "#include \"player.h\"\nvoid move_and_draw(void) {}\n",
  },
  projectMainSource,
  projectStaticA,
  {
    name: "duplicate.c",
    source: "#include \"player.h\"\n\nvoid move_and_draw(void) {}\n",
  },
];
const duplicateResult = compiler.analyzeProjectSource(projectMainSource, {
  projectRevision: 4,
  projectSources: duplicateProjectSources,
  headers: projectHeaders,
  cursor: {
    sourceName: projectMainSource.name,
    byteOffset: projectUseStart + 2,
  },
});
if (duplicateResult.hover?.definition !== null ||
    !duplicateResult.hover?.definitionConflict ||
    duplicateResult.hover?.definitionCandidates.length !== 2) {
  throw new Error(
    `duplicate project definitions were not explicit: ${JSON.stringify(duplicateResult)}`,
  );
}

assert.throws(
  () => compiler.analyzeProjectSource(projectMainSource, {
    projectRevision: 5,
    projectSources,
    headers: projectHeaders,
    limits: { maxSources: 1 },
    cursor: {
      sourceName: projectMainSource.name,
      byteOffset: projectUseStart + 2,
    },
  }),
  (error) => error?.name === "AgcResourceLimitError" &&
    error.limit === "maxSources",
);
assert.throws(
  () => compiler.analyzeProjectSource(
    { name: "first.c", source: "void first_indexed(void) {}\n" },
    {
      projectRevision: 6,
      projectSources: [
        { name: "first.c", source: "void first_indexed(void) {}\n" },
        { name: "second.c", source: "void second_indexed(void) {}\n" },
      ],
      limits: { maxAnalysisSymbols: 1 },
      cursor: { sourceName: "first.c", byteOffset: 6 },
    },
  ),
  (error) => error?.name === "AgcResourceLimitError" &&
    error.limit === "maxAnalysisSymbols",
);
assert.throws(
  () => compiler.analyzeProjectSource(projectMainSource, {
    projectRevision: 7,
    projectSources: [projectMainSource, projectMainSource],
    headers: projectHeaders,
    cursor: {
      sourceName: projectMainSource.name,
      byteOffset: projectUseStart + 2,
    },
  }),
  /duplicate project source name/,
);

const recoveryCompiler = await createCompiler(wasmModule);
const recoverySource = (value) => ({
  name: "main.c",
  source:
    `enum { PLAYER_SIZE = ${value}, PLAYER_SPEED = 2 };\n` +
    "int main(void) { return PLAYER_SIZE; }\n",
});
const invalidRecoverySource = {
  name: "main.c",
  source:
    "enum { PLAYER_SIZE = , PLAYER_SPEED = 2 };\n" +
    "int main(void) { return PLAYER_SIZE; }\n",
};
const recoveryCursor = (input) => ({
  sourceName: input.name,
  byteOffset: input.source.lastIndexOf("PLAYER_SIZE") +
    Buffer.byteLength("PLAYER_SIZE"),
});
const analyzeRecoveryRevision = (revision, input, projectInputs = [input]) =>
  recoveryCompiler.analyzeProjectSource(input, {
    projectRevision: revision,
    projectSources: projectInputs,
    cursor: recoveryCursor(input),
  });
const assertProjectAnalysisFailure = (operation, expectedCode = null) => {
  assert.throws(operation, (error) => {
    if (error instanceof WebAssembly.RuntimeError ||
        error?.name !== "AgcLanguageAnalysisError" ||
        !Array.isArray(error.diagnostics) ||
        (!error.diagnostics.some((diagnostic) =>
          diagnostic.severity === "error" &&
          (!expectedCode || diagnostic.code === expectedCode)) &&
         (expectedCode || error.code === "AGC_LANGUAGE_ANALYSIS_FAILED"))) {
      return false;
    }
    return true;
  });
};
try {
  const revision1 = recoverySource(12);
  assert.equal(
    analyzeRecoveryRevision(1, revision1).hover?.initializer.constantValue,
    "12",
  );
  assertProjectAnalysisFailure(
    () => analyzeRecoveryRevision(2, invalidRecoverySource),
    "E3064",
  );
  const revision3 = recoverySource(13);
  const recoveredRevision3 = analyzeRecoveryRevision(3, revision3);
  assert.equal(recoveredRevision3.hover?.initializer.constantValue, "13");

  const freshRecoveryCompiler = await createCompiler(wasmModule);
  try {
    const freshRevision3 = freshRecoveryCompiler.analyzeProjectSource(
      revision3,
      {
        projectRevision: 3,
        projectSources: [revision3],
        cursor: recoveryCursor(revision3),
      },
    );
    assert.deepStrictEqual(
      recoveredRevision3,
      freshRevision3,
      "reused and fresh project recovery snapshots differ",
    );
  } finally {
    freshRecoveryCompiler.dispose();
  }
  const nativeRecoveredRevision3 = JSON.parse(execFileSync(
    nativeAnalysisPath,
    ["--project-failure-recovery-parity-json"],
    { encoding: "utf8" },
  ));
  assert.deepStrictEqual(
    recoveredRevision3,
    nativeRecoveredRevision3,
    "native and Wasm project recovery snapshots differ",
  );

  const validSupportA = {
    name: "support_a.c",
    source: "int support_a(void) { return 1; }\n",
  };
  const validSupportB = {
    name: "support_b.c",
    source: "int support_b(void) { return 2; }\n",
  };
  let revision = 4;
  for (const invalidIndex of [0, 1, 2]) {
    const invalidSources = [validSupportA, invalidRecoverySource, validSupportB];
    const invalid = invalidSources.splice(1, 1)[0];
    invalidSources.splice(invalidIndex, 0, invalid);
    assertProjectAnalysisFailure(
      () => analyzeRecoveryRevision(revision++, invalidRecoverySource,
                                    invalidSources),
      "E3064",
    );
    const valid = recoverySource(20 + invalidIndex);
    const validSources = [validSupportA, valid, validSupportB];
    assert.equal(
      analyzeRecoveryRevision(revision++, valid, validSources)
        .hover?.initializer.constantValue,
      String(20 + invalidIndex),
    );
  }

  for (const invalid of [
    { name: "main.c", source: "int broken(int value;\n" },
    { name: "main.c", source: "#error broken header state\nint value;\n" },
    { name: "main.c", source: "int main(void) { return missing_name; }\n" },
  ]) {
    assertProjectAnalysisFailure(
      () => analyzeRecoveryRevision(revision++, invalid),
    );
    const valid = recoverySource(revision);
    assert.equal(
      analyzeRecoveryRevision(revision++, valid)
        .hover?.initializer.constantValue,
      String(revision - 1),
    );
  }

  const headerRecoverySource = {
    name: "main.c",
    source:
      "#include \"recovery.h\"\n" +
      "enum { PLAYER_SIZE = 31 };\n" +
      "int main(void) { return PLAYER_SIZE; }\n",
  };
  assertProjectAnalysisFailure(() =>
    recoveryCompiler.analyzeProjectSource(headerRecoverySource, {
      projectRevision: revision++,
      projectSources: [headerRecoverySource],
      headers: { "recovery.h": "#error broken project header\n" },
      cursor: recoveryCursor(headerRecoverySource),
    }));
  const recoveredHeaderResult = recoveryCompiler.analyzeProjectSource(
    headerRecoverySource,
    {
      projectRevision: revision++,
      projectSources: [headerRecoverySource],
      headers: { "recovery.h": "#define RECOVERY_READY 1\n" },
      cursor: recoveryCursor(headerRecoverySource),
    },
  );
  assert.equal(
    recoveredHeaderResult.hover?.initializer.constantValue,
    "31",
  );

  for (let iteration = 0; iteration < 4; iteration++) {
    assertProjectAnalysisFailure(
      () => analyzeRecoveryRevision(revision++, invalidRecoverySource),
      "E3064",
    );
    const valid = recoverySource(40 + iteration);
    analyzeRecoveryRevision(revision++, valid);
  }
  const recoveryWarmPages = recoveryCompiler.memory.buffer.byteLength / 65536;
  for (let iteration = 0; iteration < 20; iteration++) {
    assertProjectAnalysisFailure(
      () => analyzeRecoveryRevision(revision++, invalidRecoverySource),
      "E3064",
    );
    const valid = recoverySource(50 + iteration);
    const result = analyzeRecoveryRevision(revision++, valid);
    assert.equal(
      result.hover?.initializer.constantValue,
      String(50 + iteration),
    );
    assert.deepStrictEqual(
      result.diagnostics,
      recoveredRevision3.diagnostics,
      "recovered project diagnostics accumulated stale failures",
    );
  }
  assert.equal(
    recoveryCompiler.memory.buffer.byteLength / 65536,
    recoveryWarmPages,
    "Wasm memory grew across recovered project analysis failures",
  );
} finally {
  recoveryCompiler.dispose();
}

const guardedProjectHeaders = {
  "move.h":
    "#ifndef MOVE_H\n" +
    "#define MOVE_H\n\n" +
    "/* 日本語 */\n" +
    "void move_and_draw(void);\n\n" +
    "#endif\n",
  "other.h":
    "#ifndef OTHER_H\n" +
    "#define OTHER_H\n" +
    "void other_action(void);\n" +
    "#endif\n",
};
const guardedMoveSource = {
  name: "move.c",
  source: "#include \"move.h\"\n\nvoid move_and_draw(void) {}\n",
};
const guardedOtherSource = {
  name: "other.c",
  source: "#include \"other.h\"\n\nvoid other_action(void) {}\n",
};
const guardedMainSource = {
  name: "main.c",
  source:
    "#include \"move.h\"\n" +
    "#include \"other.h\"\n\n" +
    "int main(void) { move_and_draw(); other_action(); return 0; }\n",
};
const guardedHeaderSource = {
  name: "move.h",
  source: guardedProjectHeaders["move.h"],
};
const guardedOtherHeaderSource = {
  name: "other.h",
  source: guardedProjectHeaders["other.h"],
};
const guardedProjectSources = [
  guardedMoveSource,
  guardedOtherSource,
  guardedMainSource,
];
const guardedDeclarationStart = Buffer.byteLength(
  guardedHeaderSource.source.slice(
    0, guardedHeaderSource.source.indexOf(moveName),
  ),
);
const guardedDefinitionStart = Buffer.byteLength(
  guardedMoveSource.source.slice(
    0, guardedMoveSource.source.indexOf(moveName),
  ),
);
const wasmGuardedProjectParity = compiler.analyzeProjectSource(
  guardedHeaderSource,
  {
    projectRevision: 34,
    projectSources: guardedProjectSources,
    headers: guardedProjectHeaders,
    cursor: {
      sourceName: guardedHeaderSource.name,
      byteOffset: guardedDeclarationStart + 1,
    },
  },
);
const nativeGuardedProjectParity = JSON.parse(execFileSync(
  nativeAnalysisPath,
  ["--project-header-guard-parity-json"],
  { encoding: "utf8" },
));
assert.deepStrictEqual(
  wasmGuardedProjectParity,
  nativeGuardedProjectParity,
  "native and Wasm guarded-header project snapshots differ",
);

for (const cursorDelta of functionCursorDeltas) {
  const result = compiler.analyzeProjectSource(guardedHeaderSource, {
    projectRevision: 34,
    projectSources: guardedProjectSources,
    headers: guardedProjectHeaders,
    cursor: {
      sourceName: guardedHeaderSource.name,
      byteOffset: guardedDeclarationStart + cursorDelta,
    },
  });
  if (result.hover?.name !== moveName ||
      result.hover.declaration.sourceName !== "move.h" ||
      result.hover.declaration.start.offset !== guardedDeclarationStart ||
      result.hover.definition?.sourceName !== "move.c" ||
      result.hover.definition.start.offset !== guardedDefinitionStart ||
      result.hover.definition.end.offset !==
        guardedDefinitionStart + Buffer.byteLength(moveName) ||
      result.hover.definitionConflict ||
      result.hover.definitionCandidates.length !== 1 ||
      result.partial || result.diagnostics.length !== 0) {
    throw new Error(
      `guarded header project hover failed: ${JSON.stringify(result)}`,
    );
  }
}

const guardedMainUse = guardedMainSource.source.indexOf(moveName);
const guardedMainResult = compiler.analyzeProjectSource(guardedMainSource, {
  projectRevision: 34,
  projectSources: guardedProjectSources,
  headers: guardedProjectHeaders,
  cursor: {
    sourceName: guardedMainSource.name,
    byteOffset: guardedMainUse + 1,
  },
});
assert.equal(guardedMainResult.hover?.declaration.sourceName, "move.h");
assert.equal(guardedMainResult.hover?.definition?.sourceName, "move.c");

const otherName = "other_action";
const guardedOtherDeclarationStart = Buffer.byteLength(
  guardedOtherHeaderSource.source.slice(
    0, guardedOtherHeaderSource.source.indexOf(otherName),
  ),
);
const guardedOtherResult = compiler.analyzeProjectSource(
  guardedOtherHeaderSource,
  {
    projectRevision: 34,
    projectSources: guardedProjectSources,
    headers: guardedProjectHeaders,
    cursor: {
      sourceName: guardedOtherHeaderSource.name,
      byteOffset: guardedOtherDeclarationStart + 1,
    },
  },
);
assert.equal(guardedOtherResult.hover?.name, otherName);
assert.equal(guardedOtherResult.hover?.declaration.sourceName, "other.h");
assert.equal(guardedOtherResult.hover?.definition?.sourceName, "other.c");
assert.equal(guardedOtherResult.partial, false);
assert.deepStrictEqual(guardedOtherResult.diagnostics, []);

const movedGuardedMoveSource = {
  name: "move.c",
  source: "#include \"move.h\"\n\n\n\nvoid move_and_draw(void) {}\n",
};
const movedGuardedProjectSources = [
  movedGuardedMoveSource,
  guardedOtherSource,
  guardedMainSource,
];
const movedGuardedDefinitionStart = Buffer.byteLength(
  movedGuardedMoveSource.source.slice(
    0, movedGuardedMoveSource.source.indexOf(moveName),
  ),
);
const cachedGuardedResult = compiler.analyzeProjectSource(
  guardedHeaderSource,
  {
    projectRevision: 34,
    projectSources: movedGuardedProjectSources,
    headers: guardedProjectHeaders,
    cursor: {
      sourceName: guardedHeaderSource.name,
      byteOffset: guardedDeclarationStart + 1,
    },
  },
);
assert.equal(
  cachedGuardedResult.hover?.definition?.start.offset,
  guardedDefinitionStart,
);
const rebuiltGuardedResult = compiler.analyzeProjectSource(
  guardedHeaderSource,
  {
    projectRevision: 35,
    projectSources: movedGuardedProjectSources,
    headers: guardedProjectHeaders,
    cursor: {
      sourceName: guardedHeaderSource.name,
      byteOffset: guardedDeclarationStart + 1,
    },
  },
);
assert.equal(
  rebuiltGuardedResult.hover?.definition?.start.offset,
  movedGuardedDefinitionStart,
);

const unterminatedGuardedHeaderSource = {
  name: "move.h",
  source:
    "#ifndef MOVE_H\n" +
    "#define MOVE_H\n\n" +
    "void move_and_draw(void);\n",
};
const unterminatedDeclarationStart =
  unterminatedGuardedHeaderSource.source.indexOf(moveName);
const unterminatedGuardedResult = compiler.analyzeProjectSource(
  unterminatedGuardedHeaderSource,
  {
    projectRevision: 35,
    projectSources: movedGuardedProjectSources,
    headers: guardedProjectHeaders,
    cursor: {
      sourceName: unterminatedGuardedHeaderSource.name,
      byteOffset: unterminatedDeclarationStart + 1,
    },
  },
);
if (!unterminatedGuardedResult.partial ||
    !unterminatedGuardedResult.diagnostics.some(
      (diagnostic) => diagnostic.code === "E1053",
    )) {
  throw new Error(
    `unterminated guard lost E1053: ${JSON.stringify(unterminatedGuardedResult)}`,
  );
}
const nativeUnterminatedGuardedResult = JSON.parse(execFileSync(
  nativeAnalysisPath,
  ["--project-header-guard-error-parity-json"],
  { encoding: "utf8" },
));
assert.deepStrictEqual(
  unterminatedGuardedResult,
  nativeUnterminatedGuardedResult,
  "native and Wasm unterminated guarded-header snapshots differ",
);
const recoveredGuardedResult = compiler.analyzeProjectSource(
  guardedHeaderSource,
  {
    projectRevision: 35,
    projectSources: movedGuardedProjectSources,
    headers: guardedProjectHeaders,
    cursor: {
      sourceName: guardedHeaderSource.name,
      byteOffset: guardedDeclarationStart + 1,
    },
  },
);
assert.equal(recoveredGuardedResult.partial, false);
assert.deepStrictEqual(recoveredGuardedResult.diagnostics, []);

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
