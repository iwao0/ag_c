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
    "/// ヘビが進む方向を表します。\n" +
    "enum DocumentedDirection {\n" +
    "  /// 左へ進む方向です。\n" +
    "  DOCUMENTED_DIRECTION_LEFT,\n" +
    "  DOCUMENTED_DIRECTION_RIGHT,\n" +
    "  /**\n" +
    "   * 上へ進む方向です。\n" +
    "   *\n" +
    "   * 明示値を使用します。\n" +
    "   */\n" +
    "  DOCUMENTED_DIRECTION_UP = (1 << 2)\n" +
    "  ,\r\n" +
    "\t/// 下へ進む方向です。\r\n" +
    "\t/// CRLFでも関連付けます。\r\n" +
    "\tDOCUMENTED_DIRECTION_DOWN\r\n" +
    "};\n" +
    "int read_documented_direction(enum DocumentedDirection direction) {\n" +
    "  return direction == DOCUMENTED_DIRECTION_LEFT\n" +
    "             ? DOCUMENTED_DIRECTION_UP\n" +
    "             : DOCUMENTED_DIRECTION_RIGHT;\n" +
    "}\n" +
    "\n" +
    "/// tagだけの説明です。\n" +
    "enum TagOnlyDirection { TAG_ONLY_LEFT, TAG_ONLY_RIGHT };\n" +
    "enum ConstantOnlyDirection {\n" +
    "  /// constantだけの説明です。\n" +
    "  CONSTANT_ONLY_DIRECTION = 7\n" +
    "};\n" +
    "enum {\n" +
    "  /** anonymous constantの説明です。 */\n" +
    "  ANONYMOUS_DIRECTION = 8,\n" +
    "  ANONYMOUS_DIRECTION_UNDOCUMENTED\n" +
    "};\n" +
    "/** enum空行で切れる */\n" +
    "\n" +
    "enum BlankGapDirection { BLANK_GAP_DIRECTION };\n" +
    "/** enum通常commentで切れる */\n" +
    "/* separator */\n" +
    "enum OrdinaryGapDirection { ORDINARY_GAP_DIRECTION };\n" +
    "/** enumdirectiveで切れる */\n" +
    "#define ENUM_DOCUMENTATION_BREAK 1\n" +
    "enum DirectiveGapDirection { DIRECTIVE_GAP_DIRECTION };\n" +
    "int read_misc_directions(enum TagOnlyDirection tag_only,\n" +
    "                         enum ConstantOnlyDirection constant_only) {\n" +
    "  return tag_only == TAG_ONLY_LEFT ||\n" +
    "         constant_only == CONSTANT_ONLY_DIRECTION ||\n" +
    "         ANONYMOUS_DIRECTION;\n" +
    "}\n" +
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
    "\t/// 四角形の一辺の長さ（ピクセル）です。\r\n" +
    "\t/// プレイヤーの描画に使用します。\r\n" +
    "\t#define PLAYER_SIZE 12\r\n" +
    "\n" +
    "/**\n" +
    " * 値を二倍にします。\n" +
    " *\n" +
    " * 引数は一度だけ評価してください。\n" +
    " */\n" +
    "#define DOUBLE(value) ((value) * 2)\n" +
    "/** 継続object macro */\n" +
    "#define DOCUMENTED_LINE_OBJECT (1 + \\\n" +
    "  2)\n" +
    "/** 継続function macro */\n" +
    "#define DOCUMENTED_LINE_FUNCTION(value) ((value) + \\\n" +
    "  1)\n" +
    "/** 古いmacro説明 */\n" +
    "#define REDEFINED_DOC 1\n" +
    "#undef REDEFINED_DOC\n" +
    "/** 新しいmacro説明 */\n" +
    "#define REDEFINED_DOC 2\n" +
    "#if 0\n" +
    "/** inactive macro説明 */\n" +
    "#define INACTIVE_DOCUMENTATION 99\n" +
    "#endif\n" +
    "/** 空行で切れるmacro */\n" +
    "\n" +
    "#define BLANK_DOC_MACRO 3\n" +
    "/** 通常commentで切れるmacro */\n" +
    "/* separator */\n" +
    "#define ORDINARY_GAP_MACRO 4\n" +
    "/** 条件directiveで切れるmacro */\n" +
    "#if 1\n" +
    "#define CONDITIONAL_GAP_MACRO 5\n" +
    "#endif\n" +
    "/** pragmaで切れるmacro */\n" +
    "#pragma pack(push, 1)\n" +
    "#define PRAGMA_GAP_MACRO 6\n" +
    "#pragma pack(pop)\n" +
    "\n" +
    "int documentation_main(void) {\n" +
    "  /** local object */\n" +
    "  int local_value = 1;\n" +
    "  enemy_x = walk_frame(local_value);\n" +
    "  return enemy_x + qualified_value + left_value + right_value +\n" +
    "         external_value + prototype_only(local_value) +\n" +
    "         definition_only(local_value) + documented_both(local_value) +\n" +
    "         fallback_definition(local_value) + crlf_value + PLAYER_SIZE +\n" +
    "         DOUBLE(local_value) + DOCUMENTED_LINE_OBJECT +\n" +
    "         DOCUMENTED_LINE_FUNCTION(local_value) + REDEFINED_DOC +\n" +
    "         BLANK_DOC_MACRO + ORDINARY_GAP_MACRO +\n" +
    "         CONDITIONAL_GAP_MACRO + PRAGMA_GAP_MACRO;\n" +
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
  if (completion?.documentation !== documentationCase.documentation ||
      (documentationCase.kind === "macro" &&
       (hover.macro?.replacement !== documentationCase.replacement ||
        completion.macro?.replacement !== documentationCase.replacement ||
        JSON.stringify(hover.macro?.parameters) !==
          JSON.stringify(documentationCase.parameters) ||
        JSON.stringify(completion.macro?.parameters) !==
          JSON.stringify(documentationCase.parameters)))) {
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
  { name: "DocumentedDirection", kind: "tag",
    documentation: "ヘビが進む方向を表します。",
    comment: "/// ヘビが進む方向を表します。" },
  { name: "DOCUMENTED_DIRECTION_LEFT", kind: "enumConstant",
    documentation: "左へ進む方向です。",
    comment: "/// 左へ進む方向です。" },
  { name: "DOCUMENTED_DIRECTION_UP", kind: "enumConstant",
    documentation: "上へ進む方向です。\n\n明示値を使用します。",
    comment: "/**\n   * 上へ進む方向です。\n   *\n" +
      "   * 明示値を使用します。\n   */" },
  { name: "DOCUMENTED_DIRECTION_DOWN", kind: "enumConstant",
    documentation: "下へ進む方向です。\nCRLFでも関連付けます。",
    comment: "/// 下へ進む方向です。\r\n" +
      "\t/// CRLFでも関連付けます。" },
  { name: "TagOnlyDirection", kind: "tag",
    documentation: "tagだけの説明です。",
    comment: "/// tagだけの説明です。" },
  { name: "CONSTANT_ONLY_DIRECTION", kind: "enumConstant",
    documentation: "constantだけの説明です。",
    comment: "/// constantだけの説明です。" },
  { name: "ANONYMOUS_DIRECTION", kind: "enumConstant",
    documentation: "anonymous constantの説明です。",
    comment: "/** anonymous constantの説明です。 */" },
  { name: "first_only", kind: "object", documentation: "最初の宣言だけ",
    comment: "/** 最初の宣言だけ */" },
  { name: "crlf_value", kind: "object",
    documentation: "CRLFの説明\n二行目",
    comment: "/**\r\n\t * CRLFの説明\r\n\t * 二行目\r\n\t */" },
  { name: "local_value", kind: "object", documentation: "local object",
    comment: "/** local object */" },
  { name: "PLAYER_SIZE", kind: "macro",
    documentation: "四角形の一辺の長さ（ピクセル）です。\n" +
      "プレイヤーの描画に使用します。",
    comment: "/// 四角形の一辺の長さ（ピクセル）です。\r\n" +
      "\t/// プレイヤーの描画に使用します。",
    replacement: "12", parameters: [] },
  { name: "DOUBLE", kind: "macro",
    documentation: "値を二倍にします。\n\n引数は一度だけ評価してください。",
    comment: "/**\n * 値を二倍にします。\n *\n" +
      " * 引数は一度だけ評価してください。\n */",
    replacement: "( ( value ) * 2 )", parameters: ["value"] },
  { name: "DOCUMENTED_LINE_OBJECT", kind: "macro",
    documentation: "継続object macro", comment: "/** 継続object macro */",
    replacement: "( 1 + 2 )", parameters: [] },
  { name: "DOCUMENTED_LINE_FUNCTION", kind: "macro",
    documentation: "継続function macro",
    comment: "/** 継続function macro */",
    replacement: "( ( value ) + 1 )", parameters: ["value"] },
  { name: "REDEFINED_DOC", kind: "macro",
    documentation: "新しいmacro説明", comment: "/** 新しいmacro説明 */",
    replacement: "2", parameters: [] },
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

const stableDocumentationCases = documentationCases.filter(
  ({ name }) => [
    "enemy_x", "walk_frame", "DocumentedDirection",
    "DOCUMENTED_DIRECTION_LEFT", "DOCUMENTED_DIRECTION_UP",
    "DOCUMENTED_DIRECTION_DOWN", "TagOnlyDirection",
    "CONSTANT_ONLY_DIRECTION", "ANONYMOUS_DIRECTION",
    "PLAYER_SIZE", "DOUBLE",
  ].includes(name),
);
for (const documentationCase of stableDocumentationCases) {
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

for (const documentationCase of stableDocumentationCases) {
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

const undocumentedEnumName = "DOCUMENTED_DIRECTION_RIGHT";
for (const occurrence of [
  documentationSource.source.indexOf(undocumentedEnumName),
  documentationSource.source.lastIndexOf(undocumentedEnumName),
]) {
  for (const delta of [
    0,
    Math.floor(Buffer.byteLength(undocumentedEnumName) / 2),
    Buffer.byteLength(undocumentedEnumName),
  ]) {
    const byteOffset = documentationByteOffset(occurrence) + delta;
    const wasmResult = compiler.analyzeSource(documentationSource, {
      cursor: { sourceName: documentationSource.name, byteOffset },
    });
    assert.equal(wasmResult.hover?.name, undocumentedEnumName);
    assert.equal(wasmResult.hover?.kind, "enumConstant");
    assert.equal(wasmResult.hover?.documentation, "");
    assert.equal(wasmResult.hover?.documentationRange, null);
    assert.deepStrictEqual(wasmResult, JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--documentation-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    )), `native and Wasm undocumented enum differ at byte ${byteOffset}`);
  }
}

for (const name of [
  "blank_gap", "directive_gap", "directive_continuation_gap",
  "declaration_after", "ordinary_block", "ordinary_line", "comment_text",
  "string_after", "comment_character", "character_after",
  "DOCUMENTED_DIRECTION_RIGHT",
  "ConstantOnlyDirection", "TAG_ONLY_LEFT", "TAG_ONLY_RIGHT",
  "ANONYMOUS_DIRECTION_UNDOCUMENTED", "BlankGapDirection",
  "OrdinaryGapDirection", "DirectiveGapDirection",
  "BLANK_DOC_MACRO", "ORDINARY_GAP_MACRO", "CONDITIONAL_GAP_MACRO",
  "PRAGMA_GAP_MACRO",
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
const inactiveDocumentationResult = compiler.analyzeSource(
  documentationSource,
  {
    cursor: {
      sourceName: documentationSource.name,
      byteOffset: Buffer.byteLength(documentationSource.source),
    },
  },
);
assert.equal(
  symbol(inactiveDocumentationResult, "INACTIVE_DOCUMENTATION", "macro"),
  undefined,
);

const macroHeaderMain = {
  name: "macro-header-main.c",
  source: "#include \"macro-doc.h\"\n" +
    "int macro_header_main(void) { return HEADER_DOC(INCLUDE_GAP_MACRO); }\n",
};
const macroHeaderSources = [
  "/** include boundary */\n" +
    "#include \"empty.h\"\n" +
    "#define INCLUDE_GAP_MACRO 4\n" +
    "/// header macro v1\n" +
    "#define HEADER_DOC(value) ((value) + 1)\n",
  "/** include boundary */\n" +
    "#include \"empty.h\"\n" +
    "#define INCLUDE_GAP_MACRO 4\n" +
    "/** header macro v2 */\n" +
    "#define HEADER_DOC(value) ((value) + 2)\n",
  "/** include boundary */\n" +
    "#include \"empty.h\"\n" +
    "#define INCLUDE_GAP_MACRO 4\n" +
    "#define HEADER_DOC(value) ((value) + 3)\n",
];
const macroHeaderUse = macroHeaderMain.source.indexOf("HEADER_DOC");
for (let revision = 1; revision <= 3; revision++) {
  const header = macroHeaderSources[revision - 1];
  const wasmResult = compiler.analyzeSource(macroHeaderMain, {
    headers: { "macro-doc.h": header, "empty.h": "" },
    cursor: {
      sourceName: macroHeaderMain.name,
      byteOffset: macroHeaderUse + 3,
    },
  });
  const expectedDocumentation = revision === 1 ? "header macro v1"
    : revision === 2 ? "header macro v2" : "";
  const comment = revision === 1 ? "/// header macro v1"
    : revision === 2 ? "/** header macro v2 */" : null;
  const commentStart = comment === null ? -1 : header.indexOf(comment);
  const headerMacro = symbol(wasmResult, "HEADER_DOC", "macro");
  const includeGap = symbol(wasmResult, "INCLUDE_GAP_MACRO", "macro");
  if (wasmResult.hover?.name !== "HEADER_DOC" ||
      wasmResult.hover.documentation !== expectedDocumentation ||
      headerMacro?.documentation !== expectedDocumentation ||
      headerMacro?.macro?.parameters.join(",") !== "value" ||
      includeGap?.documentation !== "" ||
      includeGap.documentationRange !== null ||
      (comment === null
        ? headerMacro.documentationRange !== null
        : headerMacro.documentationRange?.sourceName !== "macro-doc.h" ||
          headerMacro.documentationRange.start.offset !== commentStart ||
          headerMacro.documentationRange.end.offset !==
            commentStart + Buffer.byteLength(comment))) {
    throw new Error(
      `virtual header macro documentation revision ${revision} failed: ` +
      JSON.stringify(wasmResult),
    );
  }
  const nativeResult = JSON.parse(execFileSync(
    nativeAnalysisPath,
    ["--macro-documentation-header-parity-json", String(revision)],
    { encoding: "utf8" },
  ));
  assert.deepStrictEqual(
    wasmResult,
    nativeResult,
    `native and Wasm header macro documentation differ at revision ${revision}`,
  );
  if (revision === 1) {
    const freshCompiler = await createCompiler(wasmModule);
    try {
      const freshResult = freshCompiler.analyzeSource(macroHeaderMain, {
        headers: { "macro-doc.h": header, "empty.h": "" },
        cursor: {
          sourceName: macroHeaderMain.name,
          byteOffset: macroHeaderUse + 3,
        },
      });
      assert.deepStrictEqual(
        freshResult,
        nativeResult,
        "fresh header macro documentation snapshot differs",
      );
    } finally {
      freshCompiler.dispose();
    }
  }
}

const macroProjectSources = [
  "/** project macro v1 */\n" +
    "#define PROJECT_DOC 10\n" +
    "int macro_project_main(void) { return PROJECT_DOC; }\n",
  "/** project macro v2 */\n" +
    "#define PROJECT_DOC 20\n" +
    "int macro_project_main(void) { return PROJECT_DOC; }\n",
  "#define PROJECT_DOC 30\n" +
    "int macro_project_main(void) { return PROJECT_DOC; }\n",
];
const macroProjectCompiler = await createCompiler(wasmModule);
try {
  for (let revision = 1; revision <= 3; revision++) {
    const source = {
      name: "macro-project.c",
      source: macroProjectSources[revision - 1],
    };
    const definition = source.source.indexOf("PROJECT_DOC");
    const use = source.source.indexOf("PROJECT_DOC", definition + 1);
    const result = macroProjectCompiler.analyzeProjectSource(source, {
      projectRevision: revision,
      projectSources: [source],
      cursor: { sourceName: source.name, byteOffset: use + 3 },
    });
    const expectedDocumentation = revision === 1 ? "project macro v1"
      : revision === 2 ? "project macro v2" : "";
    const projectMacro = symbol(result, "PROJECT_DOC", "macro");
    if (result.hover?.name !== "PROJECT_DOC" ||
        result.hover.documentation !== expectedDocumentation ||
        projectMacro?.documentation !== expectedDocumentation ||
        projectMacro?.macro?.replacement !== String(revision * 10) ||
        (expectedDocumentation
          ? projectMacro.documentationRange?.sourceName !== source.name
          : projectMacro.documentationRange !== null)) {
      throw new Error(
        `project macro documentation revision ${revision} failed: ` +
        JSON.stringify(result),
      );
    }
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--macro-documentation-project-parity-json", String(revision)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      result,
      nativeResult,
      `native and Wasm project macro documentation differ at revision ${revision}`,
    );
  }
} finally {
  macroProjectCompiler.dispose();
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

const limitedMacroSource = {
  name: "macro-limit.c",
  source: "/** 12345678901234 */\n#define LIMITED_MACRO 1\n",
};
try {
  compiler.analyzeSource(limitedMacroSource, {
    cursor: {
      sourceName: limitedMacroSource.name,
      byteOffset: limitedMacroSource.source.indexOf("LIMITED_MACRO") + 2,
    },
    limits: { maxAnalysisStringBytes: 13 },
  });
  throw new Error("macro documentation string limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES" ||
      error.limit !== "maxAnalysisStringBytes" || error.max !== 13 ||
      error.actual !== 14) {
    throw error;
  }
}
const macroEntryLimitSource = {
  name: "macro-entry-limit.c",
  source: "/** first */\n#define FIRST_DOC 1\n" +
    "/** second */\n#define SECOND_DOC 2\n",
};
try {
  compiler.analyzeSource(macroEntryLimitSource, {
    cursor: {
      sourceName: macroEntryLimitSource.name,
      byteOffset: Buffer.byteLength(macroEntryLimitSource.source),
    },
    limits: { maxAnalysisSymbols: 1 },
  });
  throw new Error("macro documentation entry limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS" ||
      error.limit !== "maxAnalysisSymbols" || error.max !== 1 ||
      error.actual !== 2) {
    throw error;
  }
}
const macroDocumentationAfterLimit = compiler.analyzeSource(
  { name: "macro-limit.c", source: "#define OK 1\n" },
  { cursor: { sourceName: "macro-limit.c", byteOffset: 13 } },
);
assert.equal(symbol(macroDocumentationAfterLimit, "OK", "macro")?.documentation, "");

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

const plainMacroSnapshotInput = {
  name: "macro-snapshot.c",
  source: "#define BOUNDED_MACRO 1\n" +
    "int bounded_macro_main(void) { return BOUNDED_MACRO; }\n",
};
const documentedMacroSnapshotInput = {
  name: "macro-snapshot.c",
  source: "/** bounded macro doc */\n" +
    "#define BOUNDED_MACRO 1\n" +
    "int bounded_macro_main(void) { return BOUNDED_MACRO; }\n",
};
const plainMacroSnapshotMinimum = minimumDocumentationSnapshotLimit(
  plainMacroSnapshotInput,
);
const documentedMacroSnapshotMinimum = minimumDocumentationSnapshotLimit(
  documentedMacroSnapshotInput,
);
assert.ok(documentedMacroSnapshotMinimum > plainMacroSnapshotMinimum,
  "macro documentation did not contribute to the Wasm snapshot byte limit");
assert.equal(documentationSnapshotSucceeds(
  plainMacroSnapshotInput, documentedMacroSnapshotMinimum - 1,
), true);
assert.equal(documentationSnapshotSucceeds(
  documentedMacroSnapshotInput, documentedMacroSnapshotMinimum - 1,
), false);
assert.equal(documentationSnapshotSucceeds(
  plainMacroSnapshotInput, documentedMacroSnapshotMinimum - 1,
), true, "Wasm session was not reusable after macro documentation snapshot limit");

const macroDefinitionFormsSource = {
  name: "macro-definition.c",
  source: "#define SIMPLE_MACRO 1\n" +
    "# define PARENTHESIZED_MACRO (2 + 3)\r\n" +
    "#define FUNCTION_MACRO(value, other) ((value) + (other))\n" +
    "#define EMPTY_MACRO\n" +
    "#define CONTINUED_OBJECT (1 + \\\n" +
    "  2)\n" +
    "#define CONTINUED_FUNCTION(value) ((value) + \\\r\n" +
    "  1)\r\n" +
    "??= define TRIGRAPH_HASH_MACRO 7\n" +
    "#define SPL\\\n" +
    "ICED_NAME_MACRO 8\n" +
    "#def\\\n" +
    "ine SPLIT_DEFINE_MACRO 9\n" +
    "#if 0\n" +
    "#define BRANCH_MACRO 10\n" +
    "#else\n" +
    "#define BRANCH_MACRO 11\n" +
    "#endif\n" +
    "#define REDEFINED_MACRO 12\n" +
    "#undef REDEFINED_MACRO\n" +
    "#define REDEFINED_MACRO 13\n" +
    "#pragma macro_definition_boundary\n" +
    "EMPTY_MACRO\n" +
    "enum MacroDefinitionEnum { MACRO_DEFINITION_ENUM = 1 };\n" +
    "int macro_definition_left, macro_definition_right[2];\n" +
    "int macro_definition_prototype(int value);\n" +
    "int macro_definition_function(void) {\n" +
    "  return SIMPLE_MACRO + PARENTHESIZED_MACRO +\n" +
    "         FUNCTION_MACRO(CONTINUED_OBJECT, CONTINUED_FUNCTION(1)) +\n" +
    "         TRIGRAPH_HASH_MACRO + SPLICED_NAME_MACRO +\n" +
    "         SPLIT_DEFINE_MACRO + BRANCH_MACRO + REDEFINED_MACRO;\n" +
    "}\n",
};
const macroDefinitionCases = [
  {
    name: "SIMPLE_MACRO", definition: "#define SIMPLE_MACRO 1",
    rawName: "SIMPLE_MACRO", replacement: "1", parameters: [],
  },
  {
    name: "PARENTHESIZED_MACRO",
    definition: "# define PARENTHESIZED_MACRO (2 + 3)",
    rawName: "PARENTHESIZED_MACRO", replacement: "( 2 + 3 )", parameters: [],
  },
  {
    name: "FUNCTION_MACRO",
    definition: "#define FUNCTION_MACRO(value, other) ((value) + (other))",
    rawName: "FUNCTION_MACRO", replacement: "( ( value ) + ( other ) )",
    parameters: ["value", "other"],
  },
  {
    name: "EMPTY_MACRO", definition: "#define EMPTY_MACRO",
    rawName: "EMPTY_MACRO", replacement: "", parameters: [],
  },
  {
    name: "CONTINUED_OBJECT", definition: "#define CONTINUED_OBJECT (1 + \\\n  2)",
    rawName: "CONTINUED_OBJECT", replacement: "( 1 + 2 )", parameters: [],
  },
  {
    name: "CONTINUED_FUNCTION",
    definition: "#define CONTINUED_FUNCTION(value) ((value) + \\\r\n  1)",
    rawName: "CONTINUED_FUNCTION", replacement: "( ( value ) + 1 )",
    parameters: ["value"],
  },
  {
    name: "TRIGRAPH_HASH_MACRO", definition: "??= define TRIGRAPH_HASH_MACRO 7",
    rawName: "TRIGRAPH_HASH_MACRO", replacement: "7", parameters: [],
  },
  {
    name: "SPLICED_NAME_MACRO", definition: "#define SPL\\\nICED_NAME_MACRO 8",
    rawName: "SPL\\\nICED_NAME_MACRO", replacement: "8", parameters: [],
  },
  {
    name: "SPLIT_DEFINE_MACRO", definition: "#def\\\nine SPLIT_DEFINE_MACRO 9",
    rawName: "SPLIT_DEFINE_MACRO", replacement: "9", parameters: [],
  },
  {
    name: "BRANCH_MACRO", definition: "#define BRANCH_MACRO 11",
    rawName: "BRANCH_MACRO", replacement: "11", parameters: [],
  },
  {
    name: "REDEFINED_MACRO", definition: "#define REDEFINED_MACRO 13",
    rawName: "REDEFINED_MACRO", replacement: "13", parameters: [],
  },
];

function byteOffsetForIndex(source, index) {
  return Buffer.byteLength(source.slice(0, index));
}

function assertMacroDefinitionSnapshot(result, macroCase, label,
  requireComplete = true) {
  const completion = symbol(result, macroCase.name, "macro");
  assert.equal(result.hover?.name, macroCase.name, `${label}: hover name`);
  assert.equal(result.hover?.kind, "macro", `${label}: hover kind`);
  assert.equal(result.hover?.macro?.replacement, macroCase.replacement,
    `${label}: hover replacement`);
  assert.deepStrictEqual(result.hover?.macro?.parameters, macroCase.parameters,
    `${label}: hover parameters`);
  assert.equal(completion?.macro?.replacement, macroCase.replacement,
    `${label}: completion replacement`);
  assert.deepStrictEqual(completion?.macro?.parameters, macroCase.parameters,
    `${label}: completion parameters`);
  assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
    `${label}: hover/completion declaration`);
  if (requireComplete) {
    assert.equal(result.partial, false, `${label}: partial`);
    assert.deepStrictEqual(result.diagnostics, [], `${label}: diagnostics`);
  }
}

for (const macroCase of macroDefinitionCases) {
  const fragmentIndex = macroDefinitionFormsSource.source.indexOf(
    macroCase.definition,
  );
  const definitionIndex = macroDefinitionFormsSource.source.indexOf(
    macroCase.rawName, fragmentIndex,
  );
  assert.notEqual(definitionIndex, -1, `missing ${macroCase.name} definition`);
  const rawNameBytes = Buffer.byteLength(macroCase.rawName);
  for (const delta of [0, Math.floor(rawNameBytes / 2), rawNameBytes]) {
    const byteOffset = byteOffsetForIndex(
      macroDefinitionFormsSource.source, definitionIndex,
    ) + delta;
    const wasmResult = compiler.analyzeSource(macroDefinitionFormsSource, {
      cursor: { sourceName: macroDefinitionFormsSource.name, byteOffset },
    });
    assertMacroDefinitionSnapshot(
      wasmResult, macroCase, `${macroCase.name} definition byte ${byteOffset}`,
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--macro-definition-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      wasmResult,
      nativeResult,
      `native and Wasm macro definition differ for ${macroCase.name}`,
    );
  }
}

const oldRedefinitionIndex = macroDefinitionFormsSource.source.indexOf(
  "REDEFINED_MACRO",
  macroDefinitionFormsSource.source.indexOf("#define REDEFINED_MACRO 12"),
);
const oldRedefinitionOffset = byteOffsetForIndex(
  macroDefinitionFormsSource.source, oldRedefinitionIndex,
) + 3;
const oldRedefinitionResult = compiler.analyzeSource(
  macroDefinitionFormsSource,
  {
    cursor: {
      sourceName: macroDefinitionFormsSource.name,
      byteOffset: oldRedefinitionOffset,
    },
  },
);
assertMacroDefinitionSnapshot(oldRedefinitionResult, {
  name: "REDEFINED_MACRO", replacement: "12", parameters: [],
}, "definition before undef");
assert.deepStrictEqual(oldRedefinitionResult, JSON.parse(execFileSync(
  nativeAnalysisPath,
  ["--macro-definition-parity-json", String(oldRedefinitionOffset)],
  { encoding: "utf8" },
)), "native and Wasm definition before undef differ");

const inactiveDefinitionIndex = macroDefinitionFormsSource.source.indexOf(
  "BRANCH_MACRO",
  macroDefinitionFormsSource.source.indexOf("#define BRANCH_MACRO 10"),
);
const inactiveDefinitionOffset = byteOffsetForIndex(
  macroDefinitionFormsSource.source, inactiveDefinitionIndex,
) + 2;
const inactiveDefinitionResult = compiler.analyzeSource(
  macroDefinitionFormsSource,
  {
    cursor: {
      sourceName: macroDefinitionFormsSource.name,
      byteOffset: inactiveDefinitionOffset,
    },
  },
);
assert.equal(symbol(inactiveDefinitionResult, "BRANCH_MACRO", "macro"), undefined);
assert.notEqual(inactiveDefinitionResult.hover?.kind, "macro");
assert.equal(
  inactiveDefinitionResult.diagnostics.some(({ code }) => code === "E3016"),
  false,
);
assert.deepStrictEqual(inactiveDefinitionResult, JSON.parse(execFileSync(
  nativeAnalysisPath,
  ["--macro-definition-parity-json", String(inactiveDefinitionOffset)],
  { encoding: "utf8" },
)), "native and Wasm inactive macro definition differ");

const pragmaIndex = macroDefinitionFormsSource.source.indexOf(
  "macro_definition_boundary",
);
const pragmaOffset = byteOffsetForIndex(
  macroDefinitionFormsSource.source, pragmaIndex,
) + 2;
const pragmaResult = compiler.analyzeSource(macroDefinitionFormsSource, {
  cursor: { sourceName: macroDefinitionFormsSource.name, byteOffset: pragmaOffset },
});
assert.equal(pragmaResult.hover, null);
assert.equal(pragmaResult.diagnostics.some(({ code }) => code === "E3016"), false);
assert.deepStrictEqual(pragmaResult, JSON.parse(execFileSync(
  nativeAnalysisPath,
  ["--macro-definition-parity-json", String(pragmaOffset)],
  { encoding: "utf8" },
)), "native and Wasm non-define directive differ");

for (const [source, name, replacement] of [
  ["#define BEFORE_INCOMPLETE 1\nint unfinished(", "BEFORE_INCOMPLETE", "1"],
  ["#define BEFORE_ERROR 2\nint broken = ;\n", "BEFORE_ERROR", "2"],
]) {
  const input = { name: "macro-trailing.c", source };
  const index = source.indexOf(name);
  const result = compiler.analyzeSource(input, {
    cursor: { sourceName: input.name, byteOffset: index + 2 },
  });
  assertMacroDefinitionSnapshot(result, {
    name, replacement, parameters: [],
  }, `${name} before invalid trailing source`);
}

const invalidMacroSource = {
  name: "invalid-macro.c",
  source: "#define INVALID_MACRO(value) ## value\nint after_invalid;\n",
};
let invalidMacroResult = null;
let invalidMacroError = null;
try {
  invalidMacroResult = compiler.analyzeSource(invalidMacroSource, {
    cursor: {
      sourceName: invalidMacroSource.name,
      byteOffset: invalidMacroSource.source.indexOf("INVALID_MACRO") + 2,
    },
  });
} catch (error) {
  invalidMacroError = error;
}
if (invalidMacroResult
  ? !invalidMacroResult.partial ||
    !invalidMacroResult.diagnostics.some(({ code }) =>
      code === "AGC_PARTIAL_MACRO_DEFINITION" || code === "E1031")
  : invalidMacroError?.name !== "AgcLanguageAnalysisError" ||
    !invalidMacroError.diagnostics?.some(({ code }) => code === "E1031")) {
  throw new Error(
    `invalid macro definition lost its diagnostic: ${JSON.stringify(
      invalidMacroResult || invalidMacroError,
    )}`,
  );
}

const macroDefinitionGameHeader = "#define GAME_SCREEN_WIDTH 320\n" +
  "#define GAME_SCREEN_HEIGHT 180\n" +
  "#define BUTTON_LEFT 0\n" +
  "#define BUTTON_RIGHT 1\n" +
  "#define BUTTON_UP 2\n" +
  "#define BUTTON_DOWN 3\n" +
  "#define BUTTON_A 4\n" +
  "#define COLOR_WHITE 0xffffff\n" +
  "#define RGB(red, green, blue) ((red) + (green) + (blue))\n" +
  "unsigned int random_next(void);\n" +
  "void random_seed(unsigned int seed);\n" +
  "unsigned int tick_count(void);\n" +
  "int input_pressed(int button);\n" +
  "void screen_clear(int color);\n" +
  "void draw_text(const char *text, int x, int y, int color);\n" +
  "void draw_rect(int x, int y, int width, int height, int color);\n" +
  "int game_running(void);\n";
const macroDefinitionSnake = {
  name: "snake.c",
  source: await readFile(
    "test/fixtures/language_analysis/macro_definition_snake.txt", "utf8",
  ),
};
const snakeMacroCases = [
  {
    name: "BOARD_COLUMNS", replacement: "( GAME_SCREEN_WIDTH / CELL_SIZE )",
    documentation: "盤面の横方向のマス数です。",
    comment: "/// 盤面の横方向のマス数です。",
  },
  {
    name: "BOARD_ROWS",
    replacement: "( ( GAME_SCREEN_HEIGHT - BOARD_TOP ) / CELL_SIZE )",
    documentation: "盤面の縦方向のマス数です。",
    comment: "/// 盤面の縦方向のマス数です。",
  },
  {
    name: "MAX_SNAKE_LENGTH", replacement: "( BOARD_COLUMNS * BOARD_ROWS )",
    documentation: "盤面に収まるヘビの最大の長さです。",
    comment: "/// 盤面に収まるヘビの最大の長さです。",
  },
];
for (const macroCase of snakeMacroCases) {
  const definitionIndex = macroDefinitionSnake.source.indexOf(macroCase.name);
  const useIndex = macroDefinitionSnake.source.lastIndexOf(macroCase.name);
  const commentIndex = macroDefinitionSnake.source.indexOf(macroCase.comment);
  const definitionStart = byteOffsetForIndex(
    macroDefinitionSnake.source, definitionIndex,
  );
  const commentStart = byteOffsetForIndex(
    macroDefinitionSnake.source, commentIndex,
  );
  const expectedDocumentationRange = {
    sourceName: macroDefinitionSnake.name,
    start: {
      line: macroDefinitionSnake.source.slice(0, commentIndex).split("\n").length,
      column: 1,
      offset: commentStart,
    },
    end: {
      line: macroDefinitionSnake.source.slice(0, commentIndex).split("\n").length,
      column: Buffer.byteLength(macroCase.comment) + 1,
      offset: commentStart + Buffer.byteLength(macroCase.comment),
    },
  };
  for (const delta of [
    0,
    Math.floor(Buffer.byteLength(macroCase.name) / 2),
    Buffer.byteLength(macroCase.name),
  ]) {
    const byteOffset = definitionStart + delta;
    const result = compiler.analyzeSource(macroDefinitionSnake, {
      headers: { "game.h": macroDefinitionGameHeader },
      cursor: { sourceName: macroDefinitionSnake.name, byteOffset },
    });
    assertMacroDefinitionSnapshot(result, {
      ...macroCase, parameters: [],
    }, `${macroCase.name} snake definition`);
    assert.equal(result.hover.documentation, macroCase.documentation);
    assert.deepStrictEqual(
      result.hover.documentationRange, expectedDocumentationRange,
    );
    assert.equal(result.hover.declaration.start.offset, definitionStart);
    assert.equal(
      result.hover.declaration.end.offset,
      definitionStart + Buffer.byteLength(macroCase.name),
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--macro-definition-snake-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      result,
      nativeResult,
      `native and Wasm snake macro definition differ for ${macroCase.name}`,
    );
  }
  const useOffset = byteOffsetForIndex(macroDefinitionSnake.source, useIndex) +
    Math.floor(Buffer.byteLength(macroCase.name) / 2);
  const useResult = compiler.analyzeSource(macroDefinitionSnake, {
    headers: { "game.h": macroDefinitionGameHeader },
    cursor: { sourceName: macroDefinitionSnake.name, byteOffset: useOffset },
  });
  assertMacroDefinitionSnapshot(useResult, {
    ...macroCase, parameters: [],
  }, `${macroCase.name} snake use`);
  assert.equal(useResult.hover.documentation, macroCase.documentation);
  assert.deepStrictEqual(useResult.hover.documentationRange,
    expectedDocumentationRange);
  const nativeUse = JSON.parse(execFileSync(
    nativeAnalysisPath,
    ["--macro-definition-snake-parity-json", String(useOffset)],
    { encoding: "utf8" },
  ));
  assert.deepStrictEqual(
    useResult,
    nativeUse,
    `native and Wasm snake macro use differ for ${macroCase.name}`,
  );

  const freshCompiler = await createCompiler(wasmModule);
  try {
    const freshResult = freshCompiler.analyzeSource(macroDefinitionSnake, {
      headers: { "game.h": macroDefinitionGameHeader },
      cursor: {
        sourceName: macroDefinitionSnake.name,
        byteOffset: definitionStart + Math.floor(
          Buffer.byteLength(macroCase.name) / 2,
        ),
      },
    });
    assertMacroDefinitionSnapshot(freshResult, {
      ...macroCase, parameters: [],
    }, `${macroCase.name} fresh snake definition`);
  } finally {
    freshCompiler.dispose();
  }
}

const castOperandHoverSource = {
  name: "cast-operand.c",
  source: "/// cast operand macro documentation\n" +
    "#define CAST_OPERAND_MACRO 17\n" +
    "typedef unsigned long CastSize;\n" +
    "struct CastRecord { int value; };\n" +
    "enum CastMode { CAST_MODE_VALUE = 3 };\n" +
    "static int cast_object = 5;\n" +
    "static int cast_seed = 9;\n" +
    "static int cast_choose(int value) { return value; }\n" +
    "static int (*cast_pointer)(int) = cast_choose;\n" +
    "static int cast_context(int parameter_value, int condition,\n" +
    "                        int *values, int index_value) {\n" +
    "  int simple = (int)CAST_OPERAND_MACRO;\n" +
    "  int nested = (int)((unsigned long)cast_object);\n" +
    "  int binary_rhs = cast_seed % (unsigned int)cast_object;\n" +
    "  int argument = cast_choose((const int)parameter_value);\n" +
    "  int conditional = condition ? (int)CAST_OPERAND_MACRO : 0;\n" +
    "  int subscript = values[(unsigned int)index_value];\n" +
    "  int typedef_name = (CastSize)cast_object;\n" +
    "  const volatile int *pointer = (const volatile int *)values;\n" +
    "  struct CastRecord *tag_pointer = (struct CastRecord *)values;\n" +
    "  int enum_cast = (enum CastMode)CAST_MODE_VALUE;\n" +
    "  int comment_gap = (int) /* operand gap */ CAST_OPERAND_MACRO;\n" +
    "  int splice_lf = (unsigned int) \\\n" +
    "cast_object;\n" +
    "  int splice_crlf = (unsigned int) \\\r\n" +
    "cast_object;\r\n" +
    "  int nested_cast = (int)((unsigned long)CAST_OPERAND_MACRO);\n" +
    "  int adjacent_builtin = (int)(long)cast_object;\n" +
    "  int adjacent_typedef = (CastSize)(long)cast_object;\n" +
    "  int adjacent_typedef_operand = (int)(CastSize)cast_object;\n" +
    "  int adjacent_comment = (int) /* adjacent cast */ " +
    "(long)CAST_OPERAND_MACRO;\n" +
    "  int adjacent_splice_lf = (int) \\\n" +
    "(long)cast_object;\n" +
    "  int adjacent_splice_crlf = (int) \\\r\n" +
    "(long)CAST_MODE_VALUE;\r\n" +
    "  (void)(int[CAST_MODE_VALUE]){ 1 };\n" +
    "  (void)(int[CAST_MODE_VALUE]){ 1 }[0];\n" +
    "  (void)(int (*)[CAST_MODE_VALUE])0;\n" +
    "  int normal_call = cast_choose(parameter_value) + cast_object;\n" +
    "  int parenthesized_call = (cast_choose)(parameter_value);\n" +
    "  int parenthesized_pointer_call = (cast_pointer)(parameter_value);\n" +
    "  int dereferenced_pointer_call = (*cast_pointer)(parameter_value);\n" +
    "  int addressed_call = (&cast_choose)(parameter_value);\n" +
    "  int grouped = (cast_object + cast_seed) + CAST_OPERAND_MACRO;\n" +
    "  int type_size = (int)sizeof(unsigned int) + CAST_OPERAND_MACRO;\n" +
    "  int type_align = (int)_Alignof(unsigned int) + CAST_OPERAND_MACRO;\n" +
    "  int compound = ((struct CastRecord){ 1 }).value + " +
    "CAST_OPERAND_MACRO;\n" +
    "  return simple + nested + binary_rhs + argument + conditional +\n" +
    "         subscript + typedef_name + (pointer != 0) +\n" +
    "         (tag_pointer != 0) + enum_cast +\n" +
    "         comment_gap + splice_lf + splice_crlf + nested_cast +\n" +
    "         adjacent_builtin + adjacent_typedef +\n" +
    "         adjacent_typedef_operand + adjacent_comment +\n" +
    "         adjacent_splice_lf + adjacent_splice_crlf + normal_call +\n" +
    "         parenthesized_call +\n" +
    "         parenthesized_pointer_call + dereferenced_pointer_call +\n" +
    "         addressed_call + grouped + type_size + type_align + compound;\n" +
    "}\n",
};
const castOperandCases = [
  ["simple = (int)CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["nested = (int)((unsigned long)cast_object", "cast_object", "object"],
  ["binary_rhs = cast_seed % (unsigned int)cast_object", "cast_object", "object"],
  ["argument = cast_choose((const int)parameter_value", "parameter_value", "parameter"],
  ["conditional = condition ? (int)CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["subscript = values[(unsigned int)index_value", "index_value", "parameter"],
  ["typedef_name = (CastSize)cast_object", "cast_object", "object"],
  ["pointer = (const volatile int *)values", "values", "parameter"],
  ["tag_pointer = (struct CastRecord *)values", "values", "parameter"],
  ["enum_cast = (enum CastMode)CAST_MODE_VALUE", "CAST_MODE_VALUE", "enumConstant"],
  ["comment_gap = (int) /* operand gap */ CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["splice_lf = (unsigned int) \\\ncast_object", "cast_object", "object"],
  ["splice_crlf = (unsigned int) \\\r\ncast_object", "cast_object", "object"],
  ["nested_cast = (int)((unsigned long)CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["adjacent_builtin = (int)(long)cast_object", "cast_object", "object"],
  ["adjacent_typedef = (CastSize)(long)cast_object", "cast_object", "object"],
  ["adjacent_typedef_operand = (int)(CastSize)cast_object", "cast_object", "object"],
  ["adjacent_comment = (int) /* adjacent cast */ (long)CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["adjacent_splice_lf = (int) \\\n(long)cast_object", "cast_object", "object"],
  ["adjacent_splice_crlf = (int) \\\r\n(long)CAST_MODE_VALUE", "CAST_MODE_VALUE", "enumConstant"],
  ["(void)(int[CAST_MODE_VALUE]){ 1 };", "CAST_MODE_VALUE", "enumConstant"],
  ["(void)(int[CAST_MODE_VALUE]){ 1 }[0]", "CAST_MODE_VALUE", "enumConstant"],
  ["(void)(int (*)[CAST_MODE_VALUE])0", "CAST_MODE_VALUE", "enumConstant"],
];
for (const [fragmentText, name, kind] of castOperandCases) {
  const fragmentIndex = castOperandHoverSource.source.indexOf(fragmentText);
  const useIndex = castOperandHoverSource.source.indexOf(name, fragmentIndex);
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `cast operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(castOperandHoverSource.source, useIndex);
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(castOperandHoverSource, {
      cursor: { sourceName: castOperandHoverSource.name, byteOffset },
    });
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} cast operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} cast operand diagnostics`);
    assert.equal(result.hover?.name, name, `${name} cast operand hover`);
    assert.equal(result.hover?.kind, kind, `${name} cast operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} cast operand declaration`);
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "17");
      assert.equal(result.hover?.documentation,
        "cast operand macro documentation");
    }
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--cast-operand-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(result, nativeResult,
      `native and Wasm cast operand differ for ${name} at ${delta}`);
  }
}

for (const [fragmentText, name, kind] of [
  ["normal_call = cast_choose(parameter_value) + cast_object", "cast_object", "object"],
  ["parenthesized_call = (cast_choose)(parameter_value)", "parameter_value", "parameter"],
  ["parenthesized_pointer_call = (cast_pointer)(parameter_value)", "parameter_value", "parameter"],
  ["dereferenced_pointer_call = (*cast_pointer)(parameter_value)", "parameter_value", "parameter"],
  ["addressed_call = (&cast_choose)(parameter_value)", "parameter_value", "parameter"],
  ["grouped = (cast_object + cast_seed) + CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["type_size = (int)sizeof(unsigned int) + CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["type_align = (int)_Alignof(unsigned int) + CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
  ["compound = ((struct CastRecord){ 1 }).value + CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO", "macro"],
]) {
  const fragmentIndex = castOperandHoverSource.source.indexOf(fragmentText);
  const useIndex = castOperandHoverSource.source.indexOf(name, fragmentIndex);
  const byteOffset = byteOffsetForIndex(castOperandHoverSource.source, useIndex) +
    Math.floor(Buffer.byteLength(name) / 2);
  const result = compiler.analyzeSource(castOperandHoverSource, {
    cursor: { sourceName: castOperandHoverSource.name, byteOffset },
  });
  assert.equal(result.partial, false, `${name} non-cast context partial`);
  assert.deepStrictEqual(result.diagnostics, [], `${name} non-cast diagnostics`);
  assert.equal(result.hover?.name, name, `${name} non-cast hover`);
  assert.equal(result.hover?.kind, kind, `${name} non-cast kind`);
  assert.deepStrictEqual(
    result.hover?.declaration,
    symbol(result, name, kind)?.declaration,
    `${name} non-cast declaration`,
  );
  assert.deepStrictEqual(
    result,
    JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--cast-operand-hover-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    )),
    `native and Wasm non-cast context differ for ${name}`,
  );
}

const sizeofExpressionOperandHoverSource = {
  name: "sizeof-expression-operand.c",
  source: "/// sizeof operand macro documentation\n" +
    "#define SIZEOF_OPERAND_MACRO 17\n" +
    "enum SizeofOperandMode { SIZEOF_OPERAND_ENUM = 3 };\n" +
    "static int sizeof_global = 5;\n" +
    "static int sizeof_context(int sizeof_parameter) {\n" +
    "  int sizeof_local = 9;\n" +
    "  int global_size = (int)sizeof sizeof_global;\n" +
    "  int parameter_size = (int)sizeof sizeof_parameter;\n" +
    "  int local_size = (int)sizeof sizeof_local;\n" +
    "  int macro_size = (int)sizeof SIZEOF_OPERAND_MACRO;\n" +
    "  int enum_size = (int)sizeof SIZEOF_OPERAND_ENUM;\n" +
    "  int comment_size = (int)sizeof /* operand gap */ sizeof_global;\n" +
    "  int splice_lf_size = (int)sizeof \\\n" +
    "sizeof_parameter;\n" +
    "  int splice_crlf_size = (int)sizeof \\\r\n" +
    "sizeof_local;\r\n" +
    "  return global_size + parameter_size + local_size + macro_size +\n" +
    "         enum_size + comment_size + splice_lf_size + splice_crlf_size;\n" +
    "}\n",
};
const sizeofExpressionOperandCases = [
  ["global_size = (int)sizeof sizeof_global", "sizeof_global", "object"],
  ["parameter_size = (int)sizeof sizeof_parameter", "sizeof_parameter", "parameter"],
  ["local_size = (int)sizeof sizeof_local", "sizeof_local", "object"],
  ["macro_size = (int)sizeof SIZEOF_OPERAND_MACRO", "SIZEOF_OPERAND_MACRO", "macro"],
  ["enum_size = (int)sizeof SIZEOF_OPERAND_ENUM", "SIZEOF_OPERAND_ENUM", "enumConstant"],
  ["comment_size = (int)sizeof /* operand gap */ sizeof_global", "sizeof_global", "object"],
  ["splice_lf_size = (int)sizeof \\\nsizeof_parameter", "sizeof_parameter", "parameter"],
  ["splice_crlf_size = (int)sizeof \\\r\nsizeof_local", "sizeof_local", "object"],
];
for (const [fragmentText, name, kind] of sizeofExpressionOperandCases) {
  const fragmentIndex = sizeofExpressionOperandHoverSource.source.indexOf(
    fragmentText,
  );
  const useIndex = sizeofExpressionOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `sizeof expression operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    sizeofExpressionOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(sizeofExpressionOperandHoverSource, {
      cursor: {
        sourceName: sizeofExpressionOperandHoverSource.name,
        byteOffset,
      },
    });
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} sizeof expression operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} sizeof expression operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} sizeof expression operand hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} sizeof expression operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} sizeof expression operand declaration`);
    if (kind === "object" || kind === "parameter")
      assert.equal(result.hover?.type, "int");
    if (kind === "enumConstant")
      assert.equal(result.hover?.initializer.constantValue, "3");
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "17");
      assert.equal(result.hover?.documentation,
        "sizeof operand macro documentation");
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--sizeof-expression-operand-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm sizeof expression operand differ for ${name} at ${delta}`,
    );
  }
}

const freshSizeofExpressionCompiler = await createCompiler(wasmModule);
try {
  const name = "sizeof_parameter";
  const fragmentIndex = sizeofExpressionOperandHoverSource.source.indexOf(
    "splice_lf_size = (int)sizeof \\\nsizeof_parameter",
  );
  const useIndex = sizeofExpressionOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  const result = freshSizeofExpressionCompiler.analyzeSource(
    sizeofExpressionOperandHoverSource,
    {
      cursor: {
        sourceName: sizeofExpressionOperandHoverSource.name,
        byteOffset: byteOffsetForIndex(
          sizeofExpressionOperandHoverSource.source, useIndex,
        ) + Math.floor(Buffer.byteLength(name) / 2),
      },
    },
  );
  assert.equal(result.partial, false,
    "fresh sizeof expression operand unexpectedly partial");
  assert.deepStrictEqual(result.diagnostics, [],
    "fresh sizeof expression operand diagnostics");
  assert.equal(result.hover?.name, name,
    "fresh sizeof expression operand hover");
  assert.equal(result.hover?.kind, "parameter",
    "fresh sizeof expression operand kind");
} finally {
  freshSizeofExpressionCompiler.dispose();
}

const statementKeywordOperandHoverSource = {
  name: "statement-keyword-operand.c",
  source: "/// statement operand macro documentation\n" +
    "#define STATEMENT_OPERAND_MACRO 7\n" +
    "enum StatementOperandMode { STATEMENT_OPERAND_ENUM = 3 };\n" +
    "static int statement_global = 5;\n" +
    "static int statement_return_context(int statement_parameter) {\n" +
    "  int statement_local = statement_parameter;\n" +
    "  if (statement_parameter == 0) return statement_local;\n" +
    "  if (statement_parameter == 1) return statement_global;\n" +
    "  if (statement_parameter == 2) return statement_parameter;\n" +
    "  if (statement_parameter == 3) return STATEMENT_OPERAND_MACRO;\n" +
    "  if (statement_parameter == 4) return STATEMENT_OPERAND_ENUM;\n" +
    "  if (statement_parameter == 5)\n" +
    "    return /* operand gap */ statement_global;\n" +
    "  if (statement_parameter == 6) return \\\n" +
    "statement_parameter;\n" +
    "  return \\\r\n" +
    "statement_local;\r\n" +
    "}\n" +
    "static int statement_case_context(int statement_parameter) {\n" +
    "  int statement_local = statement_parameter;\n" +
    "  switch (statement_parameter) {\n" +
    "    case STATEMENT_OPERAND_ENUM: return statement_global;\n" +
    "    case /* operand gap */ STATEMENT_OPERAND_MACRO:\n" +
    "      return statement_parameter;\n" +
    "    default: return statement_local;\n" +
    "  }\n" +
    "}\n",
};
const statementKeywordOperandCases = [
  ["return statement_local;", "statement_local", "object"],
  ["return statement_global;", "statement_global", "object"],
  ["return statement_parameter;", "statement_parameter", "parameter"],
  ["return STATEMENT_OPERAND_MACRO;", "STATEMENT_OPERAND_MACRO", "macro"],
  ["return STATEMENT_OPERAND_ENUM;", "STATEMENT_OPERAND_ENUM", "enumConstant"],
  ["return /* operand gap */ statement_global;", "statement_global", "object"],
  ["return \\\nstatement_parameter;", "statement_parameter", "parameter"],
  ["return \\\r\nstatement_local;", "statement_local", "object"],
  ["case STATEMENT_OPERAND_ENUM:", "STATEMENT_OPERAND_ENUM", "enumConstant"],
  ["case /* operand gap */ STATEMENT_OPERAND_MACRO:", "STATEMENT_OPERAND_MACRO", "macro"],
];
for (const [fragmentText, name, kind] of statementKeywordOperandCases) {
  const fragmentIndex = statementKeywordOperandHoverSource.source.indexOf(
    fragmentText,
  );
  const useIndex = statementKeywordOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `statement keyword operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    statementKeywordOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(statementKeywordOperandHoverSource, {
      cursor: {
        sourceName: statementKeywordOperandHoverSource.name,
        byteOffset,
      },
    });
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} statement keyword operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} statement keyword operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} statement keyword operand hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} statement keyword operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} statement keyword operand declaration`);
    if (kind === "object" || kind === "parameter")
      assert.equal(result.hover?.type, "int");
    if (kind === "enumConstant")
      assert.equal(result.hover?.initializer.constantValue, "3");
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "7");
      assert.equal(result.hover?.documentation,
        "statement operand macro documentation");
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--statement-keyword-operand-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm statement keyword operand differ for ${name} at ${delta}`,
    );
  }
}

const freshStatementKeywordCompiler = await createCompiler(wasmModule);
try {
  for (const [fragmentText, name, kind] of [
    ["return \\\nstatement_parameter;", "statement_parameter", "parameter"],
    ["case /* operand gap */ STATEMENT_OPERAND_MACRO:",
      "STATEMENT_OPERAND_MACRO", "macro"],
  ]) {
    const fragmentIndex = statementKeywordOperandHoverSource.source.indexOf(
      fragmentText,
    );
    const useIndex = statementKeywordOperandHoverSource.source.indexOf(
      name, fragmentIndex,
    );
    const result = freshStatementKeywordCompiler.analyzeSource(
      statementKeywordOperandHoverSource,
      {
        cursor: {
          sourceName: statementKeywordOperandHoverSource.name,
          byteOffset: byteOffsetForIndex(
            statementKeywordOperandHoverSource.source, useIndex,
          ) + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, false,
      `fresh ${name} statement keyword operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `fresh ${name} statement keyword operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `fresh ${name} statement keyword operand hover`);
    assert.equal(result.hover?.kind, kind,
      `fresh ${name} statement keyword operand kind`);
  }
} finally {
  freshStatementKeywordCompiler.dispose();
}

const statementCallOperandHoverSource = {
  name: "statement-call-operand.c",
  source: "/// statement call macro documentation\n" +
    "#define STATEMENT_CALL_MACRO(value) (7 + (value))\n" +
    "static int statement_call_target(int value) { return value; }\n" +
    "static int statement_return_call(int parameter) {\n" +
    "  if (parameter == 0) return statement_call_target(parameter);\n" +
    "  if (parameter == 1)\n" +
    "    return statement_call_target /* operand gap */ (parameter);\n" +
    "  if (parameter == 2) return statement_call_target \\\n" +
    "(parameter);\n" +
    "  return statement_call_target \\\r\n" +
    "(parameter);\r\n" +
    "}\n" +
    "static int statement_case_call(int parameter) {\n" +
    "  switch (parameter) {\n" +
    "    case STATEMENT_CALL_MACRO(0): return parameter;\n" +
    "    case STATEMENT_CALL_MACRO /* operand gap */ (1): return parameter;\n" +
    "    case STATEMENT_CALL_MACRO \\\n" +
    "(2): return parameter;\n" +
    "    case STATEMENT_CALL_MACRO \\\r\n" +
    "(3): return parameter;\r\n" +
    "    default: return 0;\n" +
    "  }\n" +
    "}\n",
};
const statementCallOperandCases = [
  ["return statement_call_target(parameter)", "statement_call_target", "function"],
  ["return statement_call_target /* operand gap */ (parameter)",
    "statement_call_target", "function"],
  ["return statement_call_target \\\n(parameter)", "statement_call_target", "function"],
  ["return statement_call_target \\\r\n(parameter)",
    "statement_call_target", "function"],
  ["case STATEMENT_CALL_MACRO(0):", "STATEMENT_CALL_MACRO", "macro"],
  ["case STATEMENT_CALL_MACRO /* operand gap */ (1):",
    "STATEMENT_CALL_MACRO", "macro"],
  ["case STATEMENT_CALL_MACRO \\\n(2):", "STATEMENT_CALL_MACRO", "macro"],
  ["case STATEMENT_CALL_MACRO \\\r\n(3):", "STATEMENT_CALL_MACRO", "macro"],
];
for (const [fragmentText, name, kind] of statementCallOperandCases) {
  const fragmentIndex = statementCallOperandHoverSource.source.indexOf(
    fragmentText,
  );
  const useIndex = statementCallOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `statement call operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    statementCallOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(statementCallOperandHoverSource, {
      cursor: {
        sourceName: statementCallOperandHoverSource.name,
        byteOffset,
      },
    });
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} statement call operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} statement call operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} statement call operand hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} statement call operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} statement call operand declaration`);
    if (kind === "function") {
      assert.equal(result.hover?.function?.returnType, "int");
      assert.equal(result.hover?.function?.hasPrototype, true);
      assert.equal(result.hover?.function?.parameters.length, 1);
      assert.notEqual(result.hover?.definition, null);
    }
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.functionLike, true);
      assert.deepStrictEqual(result.hover?.macro?.parameters, ["value"]);
      assert.equal(result.hover?.macro?.replacement, "( 7 + ( value ) )");
      assert.equal(result.hover?.documentation,
        "statement call macro documentation");
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--statement-call-operand-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm statement call operand differ for ${name} at ${delta}`,
    );
  }
}

const freshStatementCallCompiler = await createCompiler(wasmModule);
try {
  for (const [fragmentText, name, kind] of [
    ["return statement_call_target \\\n(parameter)",
      "statement_call_target", "function"],
    ["case STATEMENT_CALL_MACRO \\\r\n(3):",
      "STATEMENT_CALL_MACRO", "macro"],
  ]) {
    const fragmentIndex = statementCallOperandHoverSource.source.indexOf(
      fragmentText,
    );
    const useIndex = statementCallOperandHoverSource.source.indexOf(
      name, fragmentIndex,
    );
    const result = freshStatementCallCompiler.analyzeSource(
      statementCallOperandHoverSource,
      {
        cursor: {
          sourceName: statementCallOperandHoverSource.name,
          byteOffset: byteOffsetForIndex(
            statementCallOperandHoverSource.source, useIndex,
          ) + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, false,
      `fresh ${name} statement call operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `fresh ${name} statement call operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `fresh ${name} statement call operand hover`);
    assert.equal(result.hover?.kind, kind,
      `fresh ${name} statement call operand kind`);
  }
} finally {
  freshStatementCallCompiler.dispose();
}

const caseExpressionOperandHoverSource = {
  name: "case-expression-operand.c",
  source: "/// case expression macro documentation\n" +
    "#define CASE_EXPRESSION_MACRO 5\n" +
    "enum CaseExpressionValue {\n" +
    "  CASE_EXPRESSION_A = 2,\n" +
    "  CASE_EXPRESSION_B = 3,\n" +
    "  CASE_EXPRESSION_C = 4,\n" +
    "  CASE_EXPRESSION_CONDITION = 0\n" +
    "};\n" +
    "static int case_expression_unary(int value) {\n" +
    "  switch (value) { case -CASE_EXPRESSION_A: return 1; default: return 0; }\n" +
    "}\n" +
    "static int case_expression_binary(int value) {\n" +
    "  switch (value) { case 1 + CASE_EXPRESSION_B: return 1; default: return 0; }\n" +
    "}\n" +
    "static int case_expression_grouped(int value) {\n" +
    "  switch (value) { case (CASE_EXPRESSION_C): return 1; default: return 0; }\n" +
    "}\n" +
    "static int case_expression_second(int value) {\n" +
    "  switch (value) { case CASE_EXPRESSION_A + CASE_EXPRESSION_B: return 1; default: return 0; }\n" +
    "}\n" +
    "static int case_expression_conditional(int value) {\n" +
    "  switch (value) { case CASE_EXPRESSION_CONDITION ? CASE_EXPRESSION_B : CASE_EXPRESSION_C: return 1; default: return 0; }\n" +
    "}\n" +
    "static int case_expression_comment(int value) {\n" +
    "  switch (value) { case CASE_EXPRESSION_A /* expression gap */ + CASE_EXPRESSION_MACRO: return 1; default: return 0; }\n" +
    "}\n" +
    "static int case_expression_splice_lf(int value) {\n" +
    "  switch (value) { case CASE_EXPRESSION_A + \\\n" +
    "CASE_EXPRESSION_B: return 1; default: return 0; }\n" +
    "}\n" +
    "static int case_expression_splice_crlf(int value) {\n" +
    "  switch (value) { case CASE_EXPRESSION_A + \\\r\n" +
    "CASE_EXPRESSION_C: return 1; default: return 0; }\r\n" +
    "}\n",
};
const caseExpressionOperandCases = [
  ["case -CASE_EXPRESSION_A:", "CASE_EXPRESSION_A", "enumConstant"],
  ["case 1 + CASE_EXPRESSION_B:", "CASE_EXPRESSION_B", "enumConstant"],
  ["case (CASE_EXPRESSION_C):", "CASE_EXPRESSION_C", "enumConstant"],
  ["case CASE_EXPRESSION_A + CASE_EXPRESSION_B:",
    "CASE_EXPRESSION_B", "enumConstant"],
  ["? CASE_EXPRESSION_B : CASE_EXPRESSION_C:",
    "CASE_EXPRESSION_C", "enumConstant"],
  ["case CASE_EXPRESSION_A /* expression gap */ + CASE_EXPRESSION_MACRO:",
    "CASE_EXPRESSION_MACRO", "macro"],
  ["case CASE_EXPRESSION_A + \\\nCASE_EXPRESSION_B:",
    "CASE_EXPRESSION_B", "enumConstant"],
  ["case CASE_EXPRESSION_A + \\\r\nCASE_EXPRESSION_C:",
    "CASE_EXPRESSION_C", "enumConstant"],
];
for (const [fragmentText, name, kind] of caseExpressionOperandCases) {
  const fragmentIndex = caseExpressionOperandHoverSource.source.indexOf(
    fragmentText,
  );
  const useIndex = caseExpressionOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `case expression operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    caseExpressionOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(caseExpressionOperandHoverSource, {
      cursor: {
        sourceName: caseExpressionOperandHoverSource.name,
        byteOffset,
      },
    });
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} case expression operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} case expression operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} case expression operand hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} case expression operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} case expression operand declaration`);
    if (kind === "enumConstant")
      assert.ok(["2", "3", "4"].includes(result.hover?.initializer.constantValue));
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "5");
      assert.equal(result.hover?.documentation,
        "case expression macro documentation");
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--case-expression-operand-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm case expression operand differ for ${name} at ${delta}`,
    );
  }
}

const freshCaseExpressionCompiler = await createCompiler(wasmModule);
try {
  for (const [fragmentText, name, kind] of [
    ["? CASE_EXPRESSION_B : CASE_EXPRESSION_C:",
      "CASE_EXPRESSION_C", "enumConstant"],
    ["case CASE_EXPRESSION_A + \\\r\nCASE_EXPRESSION_C:",
      "CASE_EXPRESSION_C", "enumConstant"],
  ]) {
    const fragmentIndex = caseExpressionOperandHoverSource.source.indexOf(
      fragmentText,
    );
    const useIndex = caseExpressionOperandHoverSource.source.indexOf(
      name, fragmentIndex,
    );
    const result = freshCaseExpressionCompiler.analyzeSource(
      caseExpressionOperandHoverSource,
      {
        cursor: {
          sourceName: caseExpressionOperandHoverSource.name,
          byteOffset: byteOffsetForIndex(
            caseExpressionOperandHoverSource.source, useIndex,
          ) + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, false,
      `fresh ${name} case expression operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `fresh ${name} case expression operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `fresh ${name} case expression operand hover`);
    assert.equal(result.hover?.kind, kind,
      `fresh ${name} case expression operand kind`);
  }
} finally {
  freshCaseExpressionCompiler.dispose();
}

const enumInitializerOperandHoverSource = {
  name: "enum-initializer-operand.c",
  source: "/// enum initializer macro documentation\n" +
    "#define ENUM_INITIALIZER_MACRO 5\n" +
    "enum EnumInitializerValue {\n" +
    "  ENUM_INITIALIZER_BASE = 3,\n" +
    "  ENUM_INITIALIZER_OTHER = 4,\n" +
    "  ENUM_INITIALIZER_CONDITION = 0,\n" +
    "  ENUM_INITIALIZER_UNARY = -ENUM_INITIALIZER_BASE,\n" +
    "  ENUM_INITIALIZER_BINARY = 1 + ENUM_INITIALIZER_OTHER,\n" +
    "  ENUM_INITIALIZER_GROUPED = (ENUM_INITIALIZER_BASE),\n" +
    "  ENUM_INITIALIZER_SECOND = ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_OTHER,\n" +
    "  ENUM_INITIALIZER_CONDITIONAL = ENUM_INITIALIZER_CONDITION\n" +
    "      ? ENUM_INITIALIZER_BASE : ENUM_INITIALIZER_OTHER,\n" +
    "  ENUM_INITIALIZER_MACRO_USE = ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_MACRO,\n" +
    "  ENUM_INITIALIZER_COMMENT = ENUM_INITIALIZER_BASE /* expression gap */ + ENUM_INITIALIZER_OTHER,\n" +
    "  ENUM_INITIALIZER_SPLICE_LF = ENUM_INITIALIZER_BASE + \\\n" +
    "ENUM_INITIALIZER_OTHER,\n" +
    "  ENUM_INITIALIZER_SPLICE_CRLF = ENUM_INITIALIZER_BASE + \\\r\n" +
    "ENUM_INITIALIZER_OTHER\r\n" +
    "};\n" +
    "static int enum_initializer_block(void) {\n" +
    "  enum { ENUM_INITIALIZER_BLOCK_BASE = 7,\n" +
    "         ENUM_INITIALIZER_BLOCK_DERIVED = ENUM_INITIALIZER_BLOCK_BASE + 1 };\n" +
    "  return ENUM_INITIALIZER_BLOCK_DERIVED;\n" +
    "}\n",
};
const enumInitializerOperandCases = [
  ["ENUM_INITIALIZER_UNARY = -ENUM_INITIALIZER_BASE",
    "ENUM_INITIALIZER_BASE", "enumConstant"],
  ["ENUM_INITIALIZER_BINARY = 1 + ENUM_INITIALIZER_OTHER",
    "ENUM_INITIALIZER_OTHER", "enumConstant"],
  ["ENUM_INITIALIZER_GROUPED = (ENUM_INITIALIZER_BASE)",
    "ENUM_INITIALIZER_BASE", "enumConstant"],
  ["ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_OTHER",
    "ENUM_INITIALIZER_OTHER", "enumConstant"],
  ["? ENUM_INITIALIZER_BASE : ENUM_INITIALIZER_OTHER",
    "ENUM_INITIALIZER_OTHER", "enumConstant"],
  ["ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_MACRO",
    "ENUM_INITIALIZER_MACRO", "macro"],
  ["/* expression gap */ + ENUM_INITIALIZER_OTHER",
    "ENUM_INITIALIZER_OTHER", "enumConstant"],
  ["ENUM_INITIALIZER_BASE + \\\nENUM_INITIALIZER_OTHER",
    "ENUM_INITIALIZER_OTHER", "enumConstant"],
  ["ENUM_INITIALIZER_BASE + \\\r\nENUM_INITIALIZER_OTHER",
    "ENUM_INITIALIZER_OTHER", "enumConstant"],
  ["ENUM_INITIALIZER_BLOCK_DERIVED = ENUM_INITIALIZER_BLOCK_BASE + 1",
    "ENUM_INITIALIZER_BLOCK_BASE", "enumConstant"],
];
for (const [fragmentText, name, kind] of enumInitializerOperandCases) {
  const fragmentIndex = enumInitializerOperandHoverSource.source.indexOf(
    fragmentText,
  );
  const useIndex = enumInitializerOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `enum initializer operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    enumInitializerOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(enumInitializerOperandHoverSource, {
      cursor: {
        sourceName: enumInitializerOperandHoverSource.name,
        byteOffset,
      },
    });
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} enum initializer operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} enum initializer operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} enum initializer operand hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} enum initializer operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} enum initializer operand declaration`);
    if (kind === "enumConstant")
      assert.ok(["3", "4", "7"].includes(
        result.hover?.initializer.constantValue,
      ));
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "5");
      assert.equal(result.hover?.documentation,
        "enum initializer macro documentation");
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--enum-initializer-operand-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm enum initializer operand differ for ${name} at ${delta}`,
    );
  }
}

const freshEnumInitializerCompiler = await createCompiler(wasmModule);
try {
  for (const [fragmentText, name, kind] of [
    ["? ENUM_INITIALIZER_BASE : ENUM_INITIALIZER_OTHER",
      "ENUM_INITIALIZER_OTHER", "enumConstant"],
    ["ENUM_INITIALIZER_BASE + \\\r\nENUM_INITIALIZER_OTHER",
      "ENUM_INITIALIZER_OTHER", "enumConstant"],
    ["ENUM_INITIALIZER_BLOCK_DERIVED = ENUM_INITIALIZER_BLOCK_BASE + 1",
      "ENUM_INITIALIZER_BLOCK_BASE", "enumConstant"],
  ]) {
    const fragmentIndex = enumInitializerOperandHoverSource.source.indexOf(
      fragmentText,
    );
    const useIndex = enumInitializerOperandHoverSource.source.indexOf(
      name, fragmentIndex,
    );
    const result = freshEnumInitializerCompiler.analyzeSource(
      enumInitializerOperandHoverSource,
      {
        cursor: {
          sourceName: enumInitializerOperandHoverSource.name,
          byteOffset: byteOffsetForIndex(
            enumInitializerOperandHoverSource.source, useIndex,
          ) + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, false,
      `fresh ${name} enum initializer operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `fresh ${name} enum initializer operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `fresh ${name} enum initializer operand hover`);
    assert.equal(result.hover?.kind, kind,
      `fresh ${name} enum initializer operand kind`);
  }
} finally {
  freshEnumInitializerCompiler.dispose();
}

const incompleteEnumInitializerSources = [
  {
    name: "incomplete-enum-initializer.c",
    source: "/// incomplete enum macro documentation\n" +
      "#define INCOMPLETE_ENUM_MACRO 5\n" +
      "enum IncompleteEnumBase {\n" +
      "  INCOMPLETE_ENUM_BASE = 3,\n" +
      "  INCOMPLETE_ENUM_OTHER = 4\n" +
      "};\n" +
      "enum IncompleteEnumPending {\n" +
      "  INCOMPLETE_ENUM_DERIVED = INCOMPLETE_ENUM_BASE /* gap */ + \\\n" +
      "INCOMPLETE_ENUM_MACRO",
  },
  {
    name: "incomplete-enum-initializer.c",
    source: "enum { INCOMPLETE_ENUM_BLOCK_BASE = 7 };\n" +
      "static int incomplete_enum_block(int parameter) {\n" +
      "  int before = parameter;\n" +
      "  enum { INCOMPLETE_ENUM_BLOCK_DERIVED = " +
      "(INCOMPLETE_ENUM_BLOCK_BASE)",
  },
  {
    name: "incomplete-enum-initializer.c",
    source: "enum { INCOMPLETE_ENUM_PARTIAL_BASE = 11,\n" +
      "       INCOMPLETE_ENUM_PARTIAL_OTHER = 12 };\n" +
      "enum { INCOMPLETE_ENUM_PARTIAL_DERIVED = " +
      "INCOMPLETE_ENUM_PARTIAL_BASE + \\\r\n" +
      "INCOMPLETE_ENUM_PARTIAL_OT",
  },
  {
    name: "incomplete-enum-initializer.c",
    source: "/// incomplete enum partial macro documentation\n" +
      "#define INCOMPLETE_ENUM_PARTIAL_MACRO 13\n" +
      "enum { INCOMPLETE_ENUM_PARTIAL_MACRO_DERIVED = " +
      "/* gap */ INCOMPLETE_ENUM_PARTIAL_MAC",
  },
  {
    name: "incomplete-enum-initializer.c",
    source: "enum { INCOMPLETE_ENUM_PARTIAL_BLOCK_BASE = 14 };\n" +
      "static int incomplete_enum_partial_block(int parameter) {\n" +
      "  int before = parameter;\n" +
      "  enum { INCOMPLETE_ENUM_PARTIAL_BLOCK_DERIVED = " +
      "+INCOMPLETE_ENUM_PARTIAL_BLOCK_BA",
  },
];
const incompleteEnumInitializerCases = [
  [0, "INCOMPLETE_ENUM_BASE", "enumConstant", "3"],
  [0, "INCOMPLETE_ENUM_MACRO", "macro", ""],
  [1, "INCOMPLETE_ENUM_BLOCK_BASE", "enumConstant", "7"],
];
const partialIncompleteEnumInitializerCases = [
  [2, "INCOMPLETE_ENUM_PARTIAL_OT", "INCOMPLETE_ENUM_PARTIAL_OTHER",
    "enumConstant", "12"],
  [3, "INCOMPLETE_ENUM_PARTIAL_MAC", "INCOMPLETE_ENUM_PARTIAL_MACRO",
    "macro", ""],
  [4, "INCOMPLETE_ENUM_PARTIAL_BLOCK_BA",
    "INCOMPLETE_ENUM_PARTIAL_BLOCK_BASE", "enumConstant", "14"],
];
for (const [sourceIndex, name, kind, constantValue] of
  incompleteEnumInitializerCases) {
  const source = incompleteEnumInitializerSources[sourceIndex];
  const useIndex = source.source.lastIndexOf(name);
  const declarationIndex = source.source.indexOf(name);
  assert.ok(useIndex >= 0 && declarationIndex >= 0,
    `${name} incomplete enum initializer anchors`);
  const useStart = byteOffsetForIndex(source.source, useIndex);
  const declarationStart = byteOffsetForIndex(source.source, declarationIndex);
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(source, {
      cursor: { sourceName: source.name, byteOffset },
    });
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, true,
      `${name} incomplete enum initializer was marked complete`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} incomplete enum initializer diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} incomplete enum initializer hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} incomplete enum initializer kind`);
    assert.equal(result.hover?.declaration.start.offset, declarationStart,
      `${name} incomplete enum initializer declaration start`);
    assert.equal(result.hover?.declaration.end.offset,
      declarationStart + Buffer.byteLength(name),
      `${name} incomplete enum initializer declaration end`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} incomplete enum initializer declaration`);
    if (kind === "enumConstant")
      assert.equal(result.hover?.initializer.constantValue, constantValue);
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "5");
      assert.equal(result.hover?.documentation,
        "incomplete enum macro documentation");
    }
    if (sourceIndex === 1) {
      assert.ok(symbol(result, "parameter", "parameter"));
      assert.ok(symbol(result, "before", "object"));
    }
    const derived = symbol(
      result,
      sourceIndex === 0
        ? "INCOMPLETE_ENUM_DERIVED"
        : "INCOMPLETE_ENUM_BLOCK_DERIVED",
      "enumConstant",
    );
    assert.equal(derived?.initializer.constantValue,
      sourceIndex === 0 ? "8" : "7",
      `${name} resolved enum initializer value was not preserved`);
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--incomplete-enum-initializer-hover-parity-json",
          String(sourceIndex), String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm incomplete enum initializer differ for ${name} at ${delta}`,
    );
  }
}

for (const [sourceIndex, partialName, completionName, kind, constantValue] of
  partialIncompleteEnumInitializerCases) {
  const source = incompleteEnumInitializerSources[sourceIndex];
  const partialIndex = source.source.lastIndexOf(partialName);
  const declarationIndex = source.source.indexOf(completionName);
  assert.ok(partialIndex >= 0 && declarationIndex >= 0,
    `${partialName} partial incomplete enum initializer anchors`);
  const partialStart = byteOffsetForIndex(source.source, partialIndex);
  const declarationStart = byteOffsetForIndex(source.source, declarationIndex);
  for (const delta of [
    0,
    Math.floor(Buffer.byteLength(partialName) / 2),
    Buffer.byteLength(partialName),
  ]) {
    const byteOffset = partialStart + delta;
    const result = compiler.analyzeSource(source, {
      cursor: { sourceName: source.name, byteOffset },
    });
    const completion = symbol(result, completionName, kind);
    assert.equal(result.partial, true,
      `${partialName} partial enum identifier was marked complete`);
    assert.equal(result.hover, null,
      `${partialName} partial enum identifier unexpectedly hovered`);
    assert.equal(result.diagnostics.length, 1,
      `${partialName} partial enum identifier diagnostic count`);
    assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
    assert.equal(result.diagnostics[0].severity, "note");
    assert.equal(result.diagnostics[0].start.offset, partialStart);
    assert.equal(result.diagnostics[0].end.offset,
      partialStart + Buffer.byteLength(partialName));
    assert.equal(completion?.declaration.start.offset, declarationStart,
      `${completionName} partial enum completion declaration start`);
    assert.equal(completion?.declaration.end.offset,
      declarationStart + Buffer.byteLength(completionName),
      `${completionName} partial enum completion declaration end`);
    if (kind === "enumConstant")
      assert.equal(completion?.initializer.constantValue, constantValue);
    if (kind === "macro") {
      assert.equal(completion?.macro?.replacement, "13");
      assert.equal(completion?.documentation,
        "incomplete enum partial macro documentation");
    }
    if (sourceIndex === 4) {
      assert.ok(symbol(result, "parameter", "parameter"));
      assert.ok(symbol(result, "before", "object"));
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--incomplete-enum-initializer-hover-parity-json",
          String(sourceIndex), String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm partial enum identifier differ for ${partialName} at ${delta}`,
    );
  }
}

const freshIncompleteEnumInitializerCompiler = await createCompiler(wasmModule);
try {
  for (const [sourceIndex, name, kind] of incompleteEnumInitializerCases) {
    const source = incompleteEnumInitializerSources[sourceIndex];
    const useIndex = source.source.lastIndexOf(name);
    const result = freshIncompleteEnumInitializerCompiler.analyzeSource(
      source,
      {
        cursor: {
          sourceName: source.name,
          byteOffset: byteOffsetForIndex(source.source, useIndex) +
            Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, true,
      `fresh ${name} incomplete enum initializer was marked complete`);
    assert.deepStrictEqual(result.diagnostics, []);
    assert.equal(result.hover?.name, name);
    assert.equal(result.hover?.kind, kind);
  }
  for (const [sourceIndex, partialName, completionName, kind] of
    partialIncompleteEnumInitializerCases) {
    const source = incompleteEnumInitializerSources[sourceIndex];
    const partialIndex = source.source.lastIndexOf(partialName);
    const result = freshIncompleteEnumInitializerCompiler.analyzeSource(
      source,
      {
        cursor: {
          sourceName: source.name,
          byteOffset: byteOffsetForIndex(source.source, partialIndex) +
            Math.floor(Buffer.byteLength(partialName) / 2),
        },
      },
    );
    assert.equal(result.partial, true);
    assert.equal(result.hover, null);
    assert.equal(result.diagnostics.length, 1);
    assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
    assert.ok(symbol(result, completionName, kind));
  }
} finally {
  freshIncompleteEnumInitializerCompiler.dispose();
}

const incompleteEnumInvalidSource = {
  name: "incomplete-enum-invalid.c",
  source: "enum { INCOMPLETE_ENUM_INVALID_BASE = 3, " +
    "INCOMPLETE_ENUM_INVALID_DERIVED = INCOMPLETE_ENUM_INVALID_BASE +",
};
const incompleteEnumInvalidUse = incompleteEnumInvalidSource.source.lastIndexOf(
  "INCOMPLETE_ENUM_INVALID_BASE",
);
try {
  const result = compiler.analyzeSource(incompleteEnumInvalidSource, {
    cursor: {
      sourceName: incompleteEnumInvalidSource.name,
      byteOffset: incompleteEnumInvalidUse + 4,
    },
  });
  assert.equal(result.partial, true,
    "incomplete enum operator was marked complete");
} catch (error) {
  assert.match(error.message, /language analysis failed/);
}
const incompleteEnumReuseResult = compiler.analyzeSource(
  incompleteEnumInitializerSources[0],
  {
    cursor: {
      sourceName: incompleteEnumInitializerSources[0].name,
      byteOffset: incompleteEnumInitializerSources[0].source.lastIndexOf(
        "INCOMPLETE_ENUM_BASE",
      ) + 4,
    },
  },
);
assert.equal(incompleteEnumReuseResult.partial, true);
assert.equal(incompleteEnumReuseResult.hover?.name, "INCOMPLETE_ENUM_BASE");

const incompleteEnumHeader =
  "/// header enum documentation\n" +
  "enum IncompleteHeaderEnum {\n" +
  "  /// header enum value documentation\n" +
  "  INCOMPLETE_HEADER_ENUM_VALUE = 17\n" +
  "};\n" +
  "/// header enum macro documentation\n" +
  "#define INCOMPLETE_HEADER_ENUM_MACRO 19\n";
const incompleteEnumHeaderRevisions = [
  {
    header: incompleteEnumHeader,
    enumValue: "17",
    macroValue: "19",
    enumDocumentation: "header enum value documentation",
    enumComment: "/// header enum value documentation",
    macroDocumentation: "header enum macro documentation",
    macroComment: "/// header enum macro documentation",
  },
  {
    header: "\n/** header enum revision 2 */\n" +
      "enum IncompleteHeaderEnum {\n" +
      "  /** header enum value revision 2 */\n" +
      "  INCOMPLETE_HEADER_ENUM_VALUE = 27\n" +
      "};\n" +
      "/** header enum macro revision 2 */\n" +
      "#define INCOMPLETE_HEADER_ENUM_MACRO 29\n",
    enumValue: "27",
    macroValue: "29",
    enumDocumentation: "header enum value revision 2",
    enumComment: "/** header enum value revision 2 */",
    macroDocumentation: "header enum macro revision 2",
    macroComment: "/** header enum macro revision 2 */",
  },
  {
    header: "enum IncompleteHeaderEnum {\n" +
      "  INCOMPLETE_HEADER_ENUM_VALUE = 37\n" +
      "};\n" +
      "#define INCOMPLETE_HEADER_ENUM_MACRO 39\n",
    enumValue: "37",
    macroValue: "39",
    enumDocumentation: "",
    enumComment: null,
    macroDocumentation: "",
    macroComment: null,
  },
  {
    header: "/** renamed header enum documentation */\n" +
      "enum RenamedHeaderEnum {\n" +
      "  /** renamed header enum value documentation */\n" +
      "  RENAMED_HEADER_ENUM_VALUE = 47\n" +
      "};\n" +
      "/** renamed header enum macro documentation */\n" +
      "#define RENAMED_HEADER_ENUM_MACRO 49\n",
    enumValue: "47",
    macroValue: "49",
    enumDocumentation: "renamed header enum value documentation",
    enumComment: "/** renamed header enum value documentation */",
    macroDocumentation: "renamed header enum macro documentation",
    macroComment: "/** renamed header enum macro documentation */",
  },
  {
    header: "/// switched value macro documentation\n" +
      "#define INCOMPLETE_HEADER_ENUM_VALUE 57\n" +
      "enum SwitchedHeaderEnum {\n" +
      "  /// switched macro enum documentation\n" +
      "  INCOMPLETE_HEADER_ENUM_MACRO = 59\n" +
      "};\n",
  },
  {
    header: "/// function-like header macro documentation\n" +
      "#define INCOMPLETE_HEADER_ENUM_MACRO(left, right) 61\n",
  },
  {
    header: "/// colliding header macro documentation\n" +
      "#define COLLIDING_HEADER_SYMBOL(value) ((value) + 70)\n" +
      "enum CollidingHeaderEnum {\n" +
      "  /// colliding header enum documentation\n" +
      "  COLLIDING_HEADER_SYMBOL = 73\n" +
      "};\n",
  },
  {
    header: "enum EnumOnlyCollidingHeader {\n" +
      "  /// enum-only colliding header documentation\n" +
      "  COLLIDING_HEADER_SYMBOL = 83\n" +
      "};\n",
  },
  {
    header: "/// macro-only colliding header documentation\n" +
      "#define COLLIDING_HEADER_SYMBOL(value) ((value) + 80)\n",
  },
];
const incompleteEnumHeaderSources = [
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_VALUE",
  },
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_MACRO",
  },
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_VA",
  },
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_MA",
  },
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = RENAMED_HEADER_ENUM_VALUE",
  },
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = RENAMED_HEADER_ENUM_MACRO",
  },
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = COLLIDING_HEADER_SYMBOL",
  },
  {
    name: "incomplete-enum-header-main.c",
    source: "#include <incomplete-enum.h>\n" +
      "enum { INCOMPLETE_HEADER_DERIVED = COLLIDING_HEADER_SYMBOL(1)",
  },
];
const projectEnumMacroHeaders = [
  "/// project header macro v1\n" +
    "#define PROJECT_COLLIDING_SYMBOL(value) ((value) + 110)\n",
  "",
  "/// project header macro v2\n" +
    "#define PROJECT_COLLIDING_SYMBOL(value) ((value) + 220)\n",
  "/// project header macro v3\n" +
    "#define PROJECT_COLLIDING_SYMBOL(entry) ((entry) + 330)\n",
  "/// project header macro v4\n" +
    "#define PROJECT_COLLIDING_SYMBOL(restored) ((restored) + 440)\n",
  "/// project header macro v5\n" +
    "#define PROJECT_COLLIDING_SYMBOL(final_value) ((final_value) + 550)\n",
];
const projectEnumMacroIndexSources = [
  {
    name: "main.c",
    source: "#include <project-collision.h>\n" +
      "enum ProjectSourceCollisionV1 {\n" +
      "  /// project source enum v1\n" +
      "  PROJECT_COLLIDING_SYMBOL = 101\n};\n" +
      "int project_collision_anchor(void) { return 1; }\n",
  },
  {
    name: "main.c",
    source: "#include <project-collision.h>\n" +
      "int project_collision_anchor(void) { return 1; }\n",
  },
  {
    name: "main.c",
    source: "#include <project-collision.h>\n" +
      "enum ProjectSourceCollisionV2 {\n" +
      "  /// project source enum v2\n" +
      "  PROJECT_COLLIDING_SYMBOL = 202\n};\n" +
      "int project_collision_anchor(void) { return 2; }\n",
  },
  {
    name: "main.c",
    source: "#include <project-collision.h>\n" +
      "enum ProjectSourceRenamedV1 {\n" +
      "  /// project source renamed enum v1\n" +
      "  PROJECT_RENAMED_SYMBOL = 303\n};\n" +
      "int project_collision_anchor(void) { return 3; }\n",
  },
  {
    name: "main.c",
    source: "#include <project-collision.h>\n" +
      "enum ProjectSourceCollisionV3 {\n" +
      "  /// project source restored enum v3\n" +
      "  PROJECT_COLLIDING_SYMBOL = 303\n};\n" +
      "int project_collision_anchor(void) { return 3; }\n",
  },
  {
    name: "main.c",
    source: "#include <project-collision.h>\n" +
      "enum ProjectSourceRenamedV2 {\n" +
      "  /// project source renamed enum v2\n" +
      "  PROJECT_RENAMED_SYMBOL = 404\n};\n" +
      "int project_collision_anchor(void) { return 4; }\n",
  },
  {
    name: "main.c",
    source: "#include <project-collision.h>\n" +
      "enum ProjectSourceCollisionV4 {\n" +
      "  /// project source restored enum v4\n" +
      "  PROJECT_COLLIDING_SYMBOL = 505\n};\n" +
      "int project_collision_anchor(void) { return 5; }\n",
  },
];
const projectEnumMacroEditSources = [
  [
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV1 {\n" +
        "  /// project source enum v1\n" +
        "  PROJECT_COLLIDING_SYMBOL = 101\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
    },
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV1 {\n" +
        "  /// project source enum v1\n" +
        "  PROJECT_COLLIDING_SYMBOL = 101\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
  ],
  [
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
    },
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
  ],
  [
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV2 {\n" +
        "  /// project source enum v2\n" +
        "  PROJECT_COLLIDING_SYMBOL = 202\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
    },
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV2 {\n" +
        "  /// project source enum v2\n" +
        "  PROJECT_COLLIDING_SYMBOL = 202\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
  ],
  [
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceRenamedV1 {\n" +
        "  /// project source renamed enum v1\n" +
        "  PROJECT_RENAMED_SYMBOL = 303\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
    },
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceRenamedV1 {\n" +
        "  /// project source renamed enum v1\n" +
        "  PROJECT_RENAMED_SYMBOL = 303\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
  ],
  [
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV3 {\n" +
        "  /// project source restored enum v3\n" +
        "  PROJECT_COLLIDING_SYMBOL = 303\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
    },
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV3 {\n" +
        "  /// project source restored enum v3\n" +
        "  PROJECT_COLLIDING_SYMBOL = 303\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
  ],
  [
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceRenamedV2 {\n" +
        "  /// project source renamed enum v2\n" +
        "  PROJECT_RENAMED_SYMBOL = 404\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
    },
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceRenamedV2 {\n" +
        "  /// project source renamed enum v2\n" +
        "  PROJECT_RENAMED_SYMBOL = 404\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
  ],
  [
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV4 {\n" +
        "  /// project source restored enum v4\n" +
        "  PROJECT_COLLIDING_SYMBOL = 505\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
    },
    {
      name: "main.c",
      source: "#include <project-collision.h>\n" +
        "enum ProjectSourceCollisionV4 {\n" +
        "  /// project source restored enum v4\n" +
        "  PROJECT_COLLIDING_SYMBOL = 505\n};\n" +
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
  ],
];
function projectEnumMacroSpacedCallSource(input) {
  const useIndex = input.source.lastIndexOf("PROJECT_COLLIDING_SYMBOL");
  const nameEnd = useIndex + "PROJECT_COLLIDING_SYMBOL".length;
  assert.ok(useIndex >= 0 && input.source.slice(nameEnd) === "(1)",
    "project enum/macro compact call source");
  return {
    ...input,
    source: input.source.slice(0, nameEnd) +
      " /* project call gap */ ( /* project argument before */ 1" +
      " /* project argument after */ )",
  };
}
function projectEnumMacroIdentifierArgumentSource(
  input, declaration, argument,
) {
  const derivedPrefix = "enum { PROJECT_COLLISION_DERIVED";
  const useIndex = input.source.lastIndexOf("PROJECT_COLLIDING_SYMBOL");
  const insertionIndex = input.source.lastIndexOf(derivedPrefix);
  const nameEnd = useIndex + "PROJECT_COLLIDING_SYMBOL".length;
  assert.ok(useIndex >= 0 && insertionIndex >= 0 &&
    insertionIndex < useIndex && input.source.slice(nameEnd) === "(1)",
  "project enum/macro identifier argument source");
  return {
    ...input,
    source: input.source.slice(0, insertionIndex) + declaration +
      input.source.slice(insertionIndex, nameEnd) + `(${argument})`,
  };
}
const projectEnumMacroRevisions = [
  {
    sourceIndex: 0, headerIndex: 0,
    enumValue: "101", enumDocumentation: "project source enum v1",
    enumComment: "/// project source enum v1",
    macroReplacement: "( ( value ) + 110 )", macroInvocationValue: "111",
    macroDocumentation: "project header macro v1",
    macroComment: "/// project header macro v1",
  },
  {
    sourceIndex: 1, headerIndex: 0,
    enumValue: null, enumDocumentation: null, enumComment: null,
    macroReplacement: "( ( value ) + 110 )", macroInvocationValue: "111",
    macroDocumentation: "project header macro v1",
    macroComment: "/// project header macro v1",
  },
  {
    sourceIndex: 2, headerIndex: 0,
    enumValue: "202", enumDocumentation: "project source enum v2",
    enumComment: "/// project source enum v2",
    macroReplacement: "( ( value ) + 110 )", macroInvocationValue: "111",
    macroDocumentation: "project header macro v1",
    macroComment: "/// project header macro v1",
  },
  {
    sourceIndex: 2, headerIndex: 1,
    enumValue: "202", enumDocumentation: "project source enum v2",
    enumComment: "/// project source enum v2",
    macroReplacement: null, macroInvocationValue: null,
    macroDocumentation: null, macroComment: null,
  },
  {
    sourceIndex: 2, headerIndex: 2,
    enumValue: "202", enumDocumentation: "project source enum v2",
    enumComment: "/// project source enum v2",
    macroReplacement: "( ( value ) + 220 )", macroInvocationValue: "221",
    macroDocumentation: "project header macro v2",
    macroComment: "/// project header macro v2",
  },
  {
    sourceIndex: 2, headerIndex: 1,
    enumValue: "202", enumDocumentation: "project source enum v2",
    enumComment: "/// project source enum v2",
    macroReplacement: null, macroInvocationValue: null,
    macroDocumentation: null, macroComment: null,
  },
  {
    sourceIndex: 1, headerIndex: 1,
    enumValue: null, enumDocumentation: null, enumComment: null,
    macroReplacement: null, macroInvocationValue: null,
    macroDocumentation: null, macroComment: null,
  },
  {
    sourceIndex: 1, headerIndex: 2,
    enumValue: null, enumDocumentation: null, enumComment: null,
    macroReplacement: "( ( value ) + 220 )", macroInvocationValue: "221",
    macroDocumentation: "project header macro v2",
    macroComment: "/// project header macro v2",
  },
  {
    sourceIndex: 2, headerIndex: 2,
    enumValue: "202", enumDocumentation: "project source enum v2",
    enumComment: "/// project source enum v2",
    macroReplacement: "( ( value ) + 220 )", macroInvocationValue: "221",
    macroDocumentation: "project header macro v2",
    macroComment: "/// project header macro v2",
  },
  {
    sourceIndex: 3, headerIndex: 2,
    enumValue: null, enumDocumentation: null, enumComment: null,
    renamedEnumValue: "303",
    renamedEnumDocumentation: "project source renamed enum v1",
    renamedEnumComment: "/// project source renamed enum v1",
    macroReplacement: "( ( value ) + 220 )", macroInvocationValue: "221",
    macroDocumentation: "project header macro v2",
    macroComment: "/// project header macro v2", macroParameter: "value",
  },
  {
    sourceIndex: 3, headerIndex: 3,
    enumValue: null, enumDocumentation: null, enumComment: null,
    renamedEnumValue: "303",
    renamedEnumDocumentation: "project source renamed enum v1",
    renamedEnumComment: "/// project source renamed enum v1",
    macroReplacement: "( ( entry ) + 330 )", macroInvocationValue: "331",
    macroDocumentation: "project header macro v3",
    macroComment: "/// project header macro v3", macroParameter: "entry",
  },
  {
    sourceIndex: 4, headerIndex: 3,
    enumValue: "303", enumDocumentation: "project source restored enum v3",
    enumComment: "/// project source restored enum v3",
    renamedEnumValue: null, renamedEnumDocumentation: null,
    renamedEnumComment: null,
    macroReplacement: "( ( entry ) + 330 )", macroInvocationValue: "331",
    macroDocumentation: "project header macro v3",
    macroComment: "/// project header macro v3", macroParameter: "entry",
  },
  {
    sourceIndex: 4, headerIndex: 4,
    enumValue: "303", enumDocumentation: "project source restored enum v3",
    enumComment: "/// project source restored enum v3",
    renamedEnumValue: null, renamedEnumDocumentation: null,
    renamedEnumComment: null,
    macroReplacement: "( ( restored ) + 440 )",
    macroInvocationValue: "441",
    macroDocumentation: "project header macro v4",
    macroComment: "/// project header macro v4", macroParameter: "restored",
  },
  {
    sourceIndex: 5, headerIndex: 4,
    enumValue: null, enumDocumentation: null, enumComment: null,
    renamedEnumValue: "404",
    renamedEnumDocumentation: "project source renamed enum v2",
    renamedEnumComment: "/// project source renamed enum v2",
    macroReplacement: "( ( restored ) + 440 )",
    macroInvocationValue: "441",
    macroDocumentation: "project header macro v4",
    macroComment: "/// project header macro v4", macroParameter: "restored",
  },
  {
    sourceIndex: 6, headerIndex: 4,
    enumValue: "505", enumDocumentation: "project source restored enum v4",
    enumComment: "/// project source restored enum v4",
    renamedEnumValue: null, renamedEnumDocumentation: null,
    renamedEnumComment: null,
    macroReplacement: "( ( restored ) + 440 )",
    macroInvocationValue: "441",
    macroDocumentation: "project header macro v4",
    macroComment: "/// project header macro v4", macroParameter: "restored",
  },
  {
    sourceIndex: 6, headerIndex: 5,
    enumValue: "505", enumDocumentation: "project source restored enum v4",
    enumComment: "/// project source restored enum v4",
    renamedEnumValue: null, renamedEnumDocumentation: null,
    renamedEnumComment: null,
    macroReplacement: "( ( final_value ) + 550 )",
    macroInvocationValue: "551",
    macroDocumentation: "project header macro v5",
    macroComment: "/// project header macro v5",
    macroParameter: "final_value",
  },
];
const enumTwoArgumentCallHeader =
  "/// enum two argument function-like macro\n" +
  "#define ENUM_TWO_ARGUMENT_CALL(left, right) " +
  "((left) + (right) + 100)\n";
const enumTwoArgumentCallPrefix =
  "#include <enum-two-argument-call.h>\n" +
    "enum EnumTwoArgumentValues {\n" +
    "  /// enum two argument callee\n" +
    "  ENUM_TWO_ARGUMENT_CALL = 7,\n" +
    "  /// enum two argument first\n" +
    "  ENUM_TWO_ARGUMENT_FIRST = 1,\n" +
    "  /// enum two argument second\n" +
    "  ENUM_TWO_ARGUMENT_SECOND = 2\n" +
    "};\n" +
    "enum { ENUM_TWO_ARGUMENT_DERIVED = ";
const enumTwoArgumentCallSources = [
  "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  ,  " +
    "ENUM_TWO_ARGUMENT_SECOND  )",
  "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  " +
    "/* before comma */ , /* after comma */  " +
    "ENUM_TWO_ARGUMENT_SECOND  )",
  "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  \\\n" +
    "  ,  \\\n" +
    "  ENUM_TWO_ARGUMENT_SECOND  )",
  "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  \\\r\n" +
    "  ,  \\\r\n" +
    "  ENUM_TWO_ARGUMENT_SECOND  )",
].map((source) => ({
  name: "enum-two-argument-call.c",
  source: enumTwoArgumentCallPrefix + source,
}));
function enumTwoArgumentMacroSource(input, argumentMode, argumentRevision) {
  const enumNames = [
    "ENUM_TWO_ARGUMENT_FIRST", "ENUM_TWO_ARGUMENT_SECOND",
  ];
  const macroNames = [
    ["ENUM_TWO_ARGUMENT_FIRST_MACRO",
      "ENUM_TWO_ARGUMENT_SECOND_MACRO"],
    ["ENUM_TWO_ARGUMENT_FIRST_MACRO",
      "ENUM_TWO_ARGUMENT_SECOND_MACRO"],
    ["ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO",
      "ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO"],
    ["ENUM_TWO_ARGUMENT_FIRST_MACRO",
      "ENUM_TWO_ARGUMENT_SECOND_MACRO"],
  ];
  const declarations = [
    [
      "/// enum two argument first macro\n" +
        "#define ENUM_TWO_ARGUMENT_FIRST_MACRO 1\n",
      "/// enum two argument first macro updated\n" +
        "#define ENUM_TWO_ARGUMENT_FIRST_MACRO 11\n",
      "/// enum two argument first renamed macro\n" +
        "#define ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO 1\n",
      "",
    ],
    [
      "/// enum two argument second macro\n" +
        "#define ENUM_TWO_ARGUMENT_SECOND_MACRO 2\n",
      "/// enum two argument second macro updated\n" +
        "#define ENUM_TWO_ARGUMENT_SECOND_MACRO 12\n",
      "/// enum two argument second renamed macro\n" +
        "#define ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO 2\n",
      "",
    ],
  ];
  const index = argumentMode - 1;
  const insertionIndex = input.source.indexOf("\n") + 1;
  const useIndex = input.source.lastIndexOf(enumNames[index]);
  assert.ok(argumentMode >= 1 && argumentMode <= 2 &&
    argumentRevision >= 0 && argumentRevision <= 3 &&
    insertionIndex > 0 && useIndex > insertionIndex,
  "enum two argument macro source anchors");
  return {
    name: input.name,
    source: input.source.slice(0, insertionIndex) +
      declarations[index][argumentRevision] +
      input.source.slice(insertionIndex, useIndex) +
      macroNames[argumentRevision][index] + input.source.slice(
        useIndex + enumNames[index].length,
      ),
  };
}
function enumTwoArgumentPairedMacroSource(input, missingArgumentMode) {
  assert.ok(missingArgumentMode >= 0 && missingArgumentMode <= 3,
    "enum two argument paired macro source revision");
  const first = enumTwoArgumentMacroSource(
    input, 1, (missingArgumentMode & 1) !== 0 ? 3 : 0,
  );
  return enumTwoArgumentMacroSource(
    first, 2, (missingArgumentMode & 2) !== 0 ? 3 : 0,
  );
}
function enumTwoArgumentPairedRenameSource(
  input, renamedArgumentIndex, otherArgumentMissing,
) {
  assert.ok(renamedArgumentIndex >= 1 && renamedArgumentIndex <= 2 &&
    (otherArgumentMissing === 0 || otherArgumentMissing === 1),
  "enum two argument paired rename source revision");
  const firstRevision = renamedArgumentIndex === 1
    ? 2 : otherArgumentMissing ? 3 : 0;
  const secondRevision = renamedArgumentIndex === 2
    ? 2 : otherArgumentMissing ? 3 : 0;
  const first = enumTwoArgumentMacroSource(input, 1, firstRevision);
  return enumTwoArgumentMacroSource(first, 2, secondRevision);
}
function enumTwoArgumentPairedUpdateSource(
  input, updatedArgumentIndex, otherArgumentMissing,
) {
  assert.ok(updatedArgumentIndex >= 1 && updatedArgumentIndex <= 2 &&
    (otherArgumentMissing === 0 || otherArgumentMissing === 1),
  "enum two argument paired update source revision");
  const firstRevision = updatedArgumentIndex === 1
    ? 1 : otherArgumentMissing ? 3 : 0;
  const secondRevision = updatedArgumentIndex === 2
    ? 1 : otherArgumentMissing ? 3 : 0;
  const first = enumTwoArgumentMacroSource(input, 1, firstRevision);
  return enumTwoArgumentMacroSource(first, 2, secondRevision);
}
function enumTwoArgumentPairedRenameUpdateSource(
  input, renamedArgumentIndex, updatedArgumentMissing,
) {
  assert.ok(renamedArgumentIndex >= 1 && renamedArgumentIndex <= 2 &&
    (updatedArgumentMissing === 0 || updatedArgumentMissing === 1),
  "enum two argument paired rename update source revision");
  const firstRevision = renamedArgumentIndex === 1
    ? 2 : updatedArgumentMissing ? 3 : 1;
  const secondRevision = renamedArgumentIndex === 2
    ? 2 : updatedArgumentMissing ? 3 : 1;
  const first = enumTwoArgumentMacroSource(input, 1, firstRevision);
  return enumTwoArgumentMacroSource(first, 2, secondRevision);
}
function enumTwoArgumentBothRenamedSource(input, missingArgumentMode) {
  const renamedDeclarations = [
    "/// enum two argument first renamed macro\n" +
      "#define ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO 1\n",
    "/// enum two argument second renamed macro\n" +
      "#define ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO 2\n",
  ];
  assert.ok(missingArgumentMode >= 0 && missingArgumentMode <= 3,
    "enum two argument both renamed source revision");
  const first = enumTwoArgumentMacroSource(input, 1, 2);
  const renamed = enumTwoArgumentMacroSource(first, 2, 2);
  let source = renamed.source;
  for (let argumentIndex = 0; argumentIndex < 2; argumentIndex++) {
    if ((missingArgumentMode & (1 << argumentIndex)) === 0) continue;
    const declaration = renamedDeclarations[argumentIndex];
    assert.ok(source.includes(declaration),
      "enum two argument renamed declaration anchor");
    source = source.replace(declaration, "");
  }
  return {name: renamed.name, source};
}
const enumThreeArgumentCallHeader =
  "/// enum three argument function-like macro\n" +
  "#define ENUM_THREE_ARGUMENT_CALL(first, middle, last) " +
  "((first) + (middle) + (last) + 100)\n";
const enumThreeArgumentCallSources = [
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "/// enum three argument middle macro\n" +
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO " +
    "/* first comma */ , ENUM_THREE_ARGUMENT_MIDDLE_MACRO " +
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "/// enum three argument middle macro\n" +
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_MIDDLE_MACRO \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "enum EnumThreeArgumentMixedValues {\n" +
    "  /// enum three argument first enum\n" +
    "  ENUM_THREE_ARGUMENT_FIRST_ENUM = 4,\n" +
    "  /// enum three argument last enum\n" +
    "  ENUM_THREE_ARGUMENT_LAST_ENUM = 5\n" +
    "};\n" +
    "/// enum three argument middle macro\n" +
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_ENUM " +
    "/* first comma */ , ENUM_THREE_ARGUMENT_MIDDLE_MACRO " +
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_ENUM)",
  "#include <enum-three-argument-call.h>\n" +
    "enum EnumThreeArgumentMixedValues {\n" +
    "  /// enum three argument first enum\n" +
    "  ENUM_THREE_ARGUMENT_FIRST_ENUM = 4,\n" +
    "  /// enum three argument last enum\n" +
    "  ENUM_THREE_ARGUMENT_LAST_ENUM = 5\n" +
    "};\n" +
    "/// enum three argument middle macro\n" +
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_ENUM \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_MIDDLE_MACRO \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_LAST_ENUM)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument middle enum\n" +
    "  ENUM_THREE_ARGUMENT_MIDDLE_ENUM = 6\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO " +
    "/* first comma */ , ENUM_THREE_ARGUMENT_MIDDLE_ENUM " +
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument middle enum\n" +
    "  ENUM_THREE_ARGUMENT_MIDDLE_ENUM = 6\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_MIDDLE_ENUM \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument middle enum\n" +
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 6\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO " +
    "/* first comma */ , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM " +
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument middle enum\n" +
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 6\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument updated middle enum\n" +
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO " +
    "/* first comma */ , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM " +
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument updated middle enum\n" +
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 7\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument updated middle enum\n" +
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 11\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO " +
    "/* first comma */ , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM " +
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
  "#include <enum-three-argument-call.h>\n" +
    "/// enum three argument first macro\n" +
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 7\n" +
    "enum EnumThreeArgumentMiddleValue {\n" +
    "  /// enum three argument updated middle enum\n" +
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n" +
    "};\n" +
    "/// enum three argument last macro\n" +
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 11\n" +
    "enum { ENUM_THREE_ARGUMENT_DERIVED = " +
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM \\\r\n" +
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
].map((source) => ({name: "enum-three-argument-call.c", source}));
const enumThreeArgumentMacroNames = [
  "ENUM_THREE_ARGUMENT_FIRST_MACRO",
  "ENUM_THREE_ARGUMENT_MIDDLE_MACRO",
  "ENUM_THREE_ARGUMENT_LAST_MACRO",
];
const enumThreeArgumentMixedNames = [
  "ENUM_THREE_ARGUMENT_FIRST_ENUM",
  "ENUM_THREE_ARGUMENT_MIDDLE_MACRO",
  "ENUM_THREE_ARGUMENT_LAST_ENUM",
];
const enumThreeArgumentMiddleEnumNames = [
  "ENUM_THREE_ARGUMENT_FIRST_MACRO",
  "ENUM_THREE_ARGUMENT_MIDDLE_ENUM",
  "ENUM_THREE_ARGUMENT_LAST_MACRO",
];
const enumThreeArgumentRenamedMiddleEnumNames = [
  "ENUM_THREE_ARGUMENT_FIRST_MACRO",
  "ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM",
  "ENUM_THREE_ARGUMENT_LAST_MACRO",
];
const enumThreeArgumentMacroValues = ["1", "2", "3"];
const enumThreeArgumentMixedValues = ["4", "2", "5"];
const enumThreeArgumentMiddleEnumValues = ["1", "6", "3"];
const enumThreeArgumentUpdatedMiddleEnumValues = ["1", "9", "3"];
const enumThreeArgumentUpdatedMacroValues = ["7", "9", "11"];
const enumThreeArgumentMacroDocumentation = [
  "enum three argument first macro",
  "enum three argument middle macro",
  "enum three argument last macro",
];
const enumThreeArgumentMixedDocumentation = [
  "enum three argument first enum",
  "enum three argument middle macro",
  "enum three argument last enum",
];
const enumThreeArgumentMiddleEnumDocumentation = [
  "enum three argument first macro",
  "enum three argument middle enum",
  "enum three argument last macro",
];
const enumThreeArgumentUpdatedMiddleEnumDocumentation = [
  "enum three argument first macro",
  "enum three argument updated middle enum",
  "enum three argument last macro",
];
const enumThreeArgumentMacroComments = [
  "/// enum three argument first macro",
  "/// enum three argument middle macro",
  "/// enum three argument last macro",
];
const enumThreeArgumentMixedComments = [
  "/// enum three argument first enum",
  "/// enum three argument middle macro",
  "/// enum three argument last enum",
];
const enumThreeArgumentMiddleEnumComments = [
  "/// enum three argument first macro",
  "/// enum three argument middle enum",
  "/// enum three argument last macro",
];
const enumThreeArgumentUpdatedMiddleEnumComments = [
  "/// enum three argument first macro",
  "/// enum three argument updated middle enum",
  "/// enum three argument last macro",
];
function enumThreeArgumentMacroSource(input, missingArgumentMode) {
  const declarations = [
    ["/// enum three argument first macro\n" +
       "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n",
     "/// enum three argument first macro\n" +
       "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 7\n"],
    ["/// enum three argument middle macro\n" +
       "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n"],
    ["/// enum three argument last macro\n" +
       "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n",
     "/// enum three argument last macro\n" +
       "#define ENUM_THREE_ARGUMENT_LAST_MACRO 11\n"],
  ];
  assert.ok(missingArgumentMode >= 0 && missingArgumentMode <= 7,
    "enum three argument source revision");
  let source = input.source;
  for (let argumentIndex = 0; argumentIndex < 3; argumentIndex++) {
    if ((missingArgumentMode & (1 << argumentIndex)) === 0) continue;
    const declaration = declarations[argumentIndex].find(
      (candidate) => source.includes(candidate),
    );
    assert.ok(declaration,
      "enum three argument declaration anchor");
    source = source.replace(declaration, "");
  }
  return {name: input.name, source};
}
const incompleteEnumHeaderCases = [
  [0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
    "enumConstant", "17", "header enum value documentation", false],
  [1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
    "macro", "19", "header enum macro documentation", false],
  [2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
    "enumConstant", "17", "header enum value documentation", true],
  [3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
    "macro", "19", "header enum macro documentation", true],
];
for (const [sourceIndex, cursorName, candidateName, kind, value,
  documentation, partialIdentifier] of incompleteEnumHeaderCases) {
  const source = incompleteEnumHeaderSources[sourceIndex];
  const cursorIndex = source.source.lastIndexOf(cursorName);
  const declarationIndex = incompleteEnumHeader.indexOf(candidateName);
  assert.ok(cursorIndex >= 0 && declarationIndex >= 0,
    `${cursorName} incomplete enum header anchors`);
  const cursorStart = byteOffsetForIndex(source.source, cursorIndex);
  const declarationStart = byteOffsetForIndex(
    incompleteEnumHeader, declarationIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(cursorName) / 2),
    Buffer.byteLength(cursorName),
  ]) {
    const byteOffset = cursorStart + delta;
    const result = compiler.analyzeSource(source, {
      headers: { "incomplete-enum.h": incompleteEnumHeader },
      cursor: { sourceName: source.name, byteOffset },
    });
    const candidate = symbol(result, candidateName, kind);
    assert.equal(result.partial, true,
      `${cursorName} incomplete enum header was marked complete`);
    assert.equal(candidate?.declaration.sourceName, "incomplete-enum.h");
    assert.equal(candidate?.declaration.start.offset, declarationStart);
    assert.equal(candidate?.declaration.end.offset,
      declarationStart + Buffer.byteLength(candidateName));
    assert.equal(candidate?.documentation, documentation);
    assert.equal(candidate?.documentationRange?.sourceName,
      "incomplete-enum.h");
    assert.deepStrictEqual(result.dependencies, ["incomplete-enum.h"]);
    if (kind === "enumConstant")
      assert.equal(candidate?.initializer.constantValue, value);
    else
      assert.equal(candidate?.macro?.replacement, value);
    if (partialIdentifier) {
      assert.equal(result.hover, null);
      assert.equal(result.diagnostics.length, 1);
      assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
      assert.equal(result.diagnostics[0].start.offset, cursorStart);
      assert.equal(result.diagnostics[0].end.offset,
        cursorStart + Buffer.byteLength(cursorName));
    } else {
      assert.deepStrictEqual(result.diagnostics, []);
      assert.equal(result.hover?.name, candidateName);
      assert.deepStrictEqual(result.hover?.declaration,
        candidate?.declaration);
      const derived = symbol(
        result, "INCOMPLETE_HEADER_DERIVED", "enumConstant",
      );
      assert.equal(derived?.initializer.constantValue, value);
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--incomplete-enum-header-hover-parity-json",
          String(sourceIndex), String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm incomplete enum header differ for ${cursorName} at ${delta}`,
    );
  }
}

const freshIncompleteEnumHeaderCompiler = await createCompiler(wasmModule);
try {
  for (const [sourceIndex, cursorName, candidateName, kind, , ,
    partialIdentifier] of incompleteEnumHeaderCases) {
    const source = incompleteEnumHeaderSources[sourceIndex];
    const cursorIndex = source.source.lastIndexOf(cursorName);
    const result = freshIncompleteEnumHeaderCompiler.analyzeSource(source, {
      headers: { "incomplete-enum.h": incompleteEnumHeader },
      cursor: {
        sourceName: source.name,
        byteOffset: byteOffsetForIndex(source.source, cursorIndex) +
          Math.floor(Buffer.byteLength(cursorName) / 2),
      },
    });
    assert.equal(result.partial, true);
    assert.ok(symbol(result, candidateName, kind));
    assert.equal(result.hover?.name ?? null,
      partialIdentifier ? null : candidateName);
    assert.equal(result.diagnostics.length, partialIdentifier ? 1 : 0);
  }
} finally {
  freshIncompleteEnumHeaderCompiler.dispose();
}

const incompleteEnumHeaderRevisionCompiler = await createCompiler(wasmModule);
try {
  for (const revisionIndex of [0, 1, 2, 0]) {
    const revision = incompleteEnumHeaderRevisions[revisionIndex];
    for (const [sourceIndex, cursorName, candidateName, kind, , ,
      partialIdentifier] of incompleteEnumHeaderCases) {
      const source = incompleteEnumHeaderSources[sourceIndex];
      const cursorIndex = source.source.lastIndexOf(cursorName);
      const cursorStart = byteOffsetForIndex(source.source, cursorIndex);
      const byteOffset = cursorStart +
        Math.floor(Buffer.byteLength(cursorName) / 2);
      const declarationIndex = revision.header.indexOf(candidateName);
      const documentation = kind === "enumConstant"
        ? revision.enumDocumentation : revision.macroDocumentation;
      const expectedValue = kind === "enumConstant"
        ? revision.enumValue : revision.macroValue;
      const commentText = kind === "enumConstant"
        ? revision.enumComment : revision.macroComment;
      const commentIndex = commentText === null
        ? -1 : revision.header.indexOf(commentText);
      assert.ok(cursorIndex >= 0 && declarationIndex >= 0 &&
        (commentText === null || commentIndex >= 0),
      `${cursorName} incomplete enum header revision anchors`);
      const result = incompleteEnumHeaderRevisionCompiler.analyzeSource(
        source,
        {
          headers: { "incomplete-enum.h": revision.header },
          cursor: { sourceName: source.name, byteOffset },
        },
      );
      const candidate = symbol(result, candidateName, kind);
      assert.equal(result.partial, true);
      assert.equal(candidate?.declaration.sourceName, "incomplete-enum.h");
      assert.equal(candidate?.declaration.start.offset,
        byteOffsetForIndex(revision.header, declarationIndex));
      assert.equal(candidate?.declaration.end.offset,
        byteOffsetForIndex(revision.header, declarationIndex) +
          Buffer.byteLength(candidateName));
      assert.equal(candidate?.documentation, documentation);
      if (commentText === null) {
        assert.equal(candidate?.documentationRange, null);
      } else {
        assert.equal(candidate?.documentationRange?.sourceName,
          "incomplete-enum.h");
        assert.equal(candidate?.documentationRange?.start.offset,
          byteOffsetForIndex(revision.header, commentIndex));
        assert.equal(candidate?.documentationRange?.end.offset,
          byteOffsetForIndex(revision.header, commentIndex) +
            Buffer.byteLength(commentText));
      }
      assert.deepStrictEqual(result.dependencies, ["incomplete-enum.h"]);
      if (kind === "enumConstant")
        assert.equal(candidate?.initializer.constantValue, expectedValue);
      else
        assert.equal(candidate?.macro?.replacement, expectedValue);
      if (partialIdentifier) {
        assert.equal(result.hover, null);
        assert.equal(result.diagnostics.length, 1);
        assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
        assert.equal(result.diagnostics[0].start.offset, cursorStart);
        assert.equal(result.diagnostics[0].end.offset,
          cursorStart + Buffer.byteLength(cursorName));
      } else {
        assert.deepStrictEqual(result.diagnostics, []);
        assert.equal(result.hover?.name, candidateName);
        assert.deepStrictEqual(result.hover?.declaration,
          candidate?.declaration);
        assert.equal(symbol(
          result, "INCOMPLETE_HEADER_DERIVED", "enumConstant",
        )?.initializer.constantValue, expectedValue);
      }
      assert.deepStrictEqual(
        result,
        JSON.parse(execFileSync(
          nativeAnalysisPath,
          ["--incomplete-enum-header-revision-parity-json",
            String(revisionIndex + 1), String(sourceIndex),
            String(byteOffset)],
          { encoding: "utf8" },
        )),
        `native and Wasm incomplete enum header revision differ for ${cursorName} at revision ${revisionIndex + 1}`,
      );
    }
  }
} finally {
  incompleteEnumHeaderRevisionCompiler.dispose();
}

const incompleteEnumHeaderRenameCases = [
  [0, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "enumConstant", true, "17",
    "header enum value documentation",
    "/// header enum value documentation"],
  [0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "macro", true, "19",
    "header enum macro documentation",
    "/// header enum macro documentation"],
  [3, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "enumConstant", false],
  [3, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "macro", false],
  [3, 4, "RENAMED_HEADER_ENUM_VALUE", "enumConstant", true, "47",
    "renamed header enum value documentation",
    "/** renamed header enum value documentation */"],
  [3, 5, "RENAMED_HEADER_ENUM_MACRO", "macro", true, "49",
    "renamed header enum macro documentation",
    "/** renamed header enum macro documentation */"],
  [0, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "enumConstant", true, "17",
    "header enum value documentation",
    "/// header enum value documentation"],
  [0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "macro", true, "19",
    "header enum macro documentation",
    "/// header enum macro documentation"],
  [0, 4, "RENAMED_HEADER_ENUM_VALUE", "enumConstant", false],
  [0, 5, "RENAMED_HEADER_ENUM_MACRO", "macro", false],
];
const incompleteEnumHeaderRenameCompiler = await createCompiler(wasmModule);
try {
  for (const [revisionIndex, sourceIndex, operandName, kind, resolves,
    value, documentation, commentText] of incompleteEnumHeaderRenameCases) {
    const revision = incompleteEnumHeaderRevisions[revisionIndex];
    const source = incompleteEnumHeaderSources[sourceIndex];
    const operandIndex = source.source.lastIndexOf(operandName);
    const operandStart = byteOffsetForIndex(source.source, operandIndex);
    const byteOffset = operandStart +
      Math.floor(Buffer.byteLength(operandName) / 2);
    assert.ok(operandIndex >= 0,
      `${operandName} incomplete enum header rename anchor`);
    const result = incompleteEnumHeaderRenameCompiler.analyzeSource(source, {
      headers: { "incomplete-enum.h": revision.header },
      cursor: { sourceName: source.name, byteOffset },
    });
    const candidate = symbol(result, operandName, kind);
    assert.equal(result.partial, true);
    assert.deepStrictEqual(result.dependencies, ["incomplete-enum.h"]);
    if (resolves) {
      const declarationIndex = revision.header.indexOf(operandName);
      const commentIndex = revision.header.indexOf(commentText);
      assert.ok(declarationIndex >= 0 && commentIndex >= 0);
      assert.deepStrictEqual(result.diagnostics, []);
      assert.equal(result.hover?.name, operandName);
      assert.deepStrictEqual(result.hover?.declaration,
        candidate?.declaration);
      assert.equal(candidate?.declaration.sourceName, "incomplete-enum.h");
      assert.equal(candidate?.declaration.start.offset,
        byteOffsetForIndex(revision.header, declarationIndex));
      assert.equal(candidate?.declaration.end.offset,
        byteOffsetForIndex(revision.header, declarationIndex) +
          Buffer.byteLength(operandName));
      assert.equal(candidate?.documentation, documentation);
      assert.equal(candidate?.documentationRange?.sourceName,
        "incomplete-enum.h");
      assert.equal(candidate?.documentationRange?.start.offset,
        byteOffsetForIndex(revision.header, commentIndex));
      assert.equal(candidate?.documentationRange?.end.offset,
        byteOffsetForIndex(revision.header, commentIndex) +
          Buffer.byteLength(commentText));
      if (kind === "enumConstant")
        assert.equal(candidate?.initializer.constantValue, value);
      else
        assert.equal(candidate?.macro?.replacement, value);
      assert.equal(symbol(
        result, "INCOMPLETE_HEADER_DERIVED", "enumConstant",
      )?.initializer.constantValue, value);
    } else {
      assert.equal(candidate, undefined);
      assert.equal(result.hover, null);
      assert.equal(result.diagnostics.length, 1);
      assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
      assert.equal(result.diagnostics[0].start.offset, operandStart);
      assert.equal(result.diagnostics[0].end.offset,
        operandStart + Buffer.byteLength(operandName));
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--incomplete-enum-header-revision-parity-json",
          String(revisionIndex + 1), String(sourceIndex), String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm incomplete enum header rename differ for ${operandName} at revision ${revisionIndex + 1}`,
    );
  }
} finally {
  incompleteEnumHeaderRenameCompiler.dispose();
}

const incompleteEnumHeaderKindCases = [
  [0, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
    "enumConstant", "17", "header enum value documentation",
    "/// header enum value documentation", false],
  [0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
    "macro", "19", "header enum macro documentation",
    "/// header enum macro documentation", false],
  [0, 2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
    "enumConstant", "17", "header enum value documentation",
    "/// header enum value documentation", true],
  [0, 3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
    "macro", "19", "header enum macro documentation",
    "/// header enum macro documentation", true],
  [4, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
    "macro", "57", "switched value macro documentation",
    "/// switched value macro documentation", false],
  [4, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
    "enumConstant", "59", "switched macro enum documentation",
    "/// switched macro enum documentation", false],
  [4, 2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
    "macro", "57", "switched value macro documentation",
    "/// switched value macro documentation", true],
  [4, 3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
    "enumConstant", "59", "switched macro enum documentation",
    "/// switched macro enum documentation", true],
  [0, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
    "enumConstant", "17", "header enum value documentation",
    "/// header enum value documentation", false],
  [0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
    "macro", "19", "header enum macro documentation",
    "/// header enum macro documentation", false],
  [0, 2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
    "enumConstant", "17", "header enum value documentation",
    "/// header enum value documentation", true],
  [0, 3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
    "macro", "19", "header enum macro documentation",
    "/// header enum macro documentation", true],
];
const incompleteEnumHeaderKindCompiler = await createCompiler(wasmModule);
try {
  for (const [revisionIndex, sourceIndex, cursorName, candidateName, kind,
    value, documentation, commentText, partialIdentifier] of
    incompleteEnumHeaderKindCases) {
    const revision = incompleteEnumHeaderRevisions[revisionIndex];
    const source = incompleteEnumHeaderSources[sourceIndex];
    const cursorIndex = source.source.lastIndexOf(cursorName);
    const cursorStart = byteOffsetForIndex(source.source, cursorIndex);
    const byteOffset = cursorStart +
      Math.floor(Buffer.byteLength(cursorName) / 2);
    const declarationIndex = revision.header.indexOf(candidateName);
    const commentIndex = revision.header.indexOf(commentText);
    assert.ok(cursorIndex >= 0 && declarationIndex >= 0 && commentIndex >= 0,
      `${cursorName} incomplete enum header kind anchors`);
    const result = incompleteEnumHeaderKindCompiler.analyzeSource(source, {
      headers: { "incomplete-enum.h": revision.header },
      cursor: { sourceName: source.name, byteOffset },
    });
    const candidate = symbol(result, candidateName, kind);
    const previousKind = kind === "enumConstant" ? "macro" : "enumConstant";
    assert.equal(result.partial, true);
    assert.ok(candidate);
    assert.equal(symbol(result, candidateName, previousKind), undefined);
    assert.equal(candidate?.declaration.sourceName, "incomplete-enum.h");
    assert.equal(candidate?.declaration.start.offset,
      byteOffsetForIndex(revision.header, declarationIndex));
    assert.equal(candidate?.declaration.end.offset,
      byteOffsetForIndex(revision.header, declarationIndex) +
        Buffer.byteLength(candidateName));
    assert.equal(candidate?.documentation, documentation);
    assert.equal(candidate?.documentationRange?.sourceName,
      "incomplete-enum.h");
    assert.equal(candidate?.documentationRange?.start.offset,
      byteOffsetForIndex(revision.header, commentIndex));
    assert.equal(candidate?.documentationRange?.end.offset,
      byteOffsetForIndex(revision.header, commentIndex) +
        Buffer.byteLength(commentText));
    assert.deepStrictEqual(result.dependencies, ["incomplete-enum.h"]);
    if (kind === "enumConstant") {
      assert.equal(candidate?.initializer.constantValue, value);
      assert.equal(candidate?.macro, null);
    } else {
      assert.equal(candidate?.macro?.replacement, value);
      assert.equal(candidate?.initializer.constantValue, null);
    }
    if (partialIdentifier) {
      assert.equal(result.hover, null);
      assert.equal(result.diagnostics.length, 1);
      assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
      assert.equal(result.diagnostics[0].start.offset, cursorStart);
      assert.equal(result.diagnostics[0].end.offset,
        cursorStart + Buffer.byteLength(cursorName));
    } else {
      assert.deepStrictEqual(result.diagnostics, []);
      assert.equal(result.hover?.name, candidateName);
      assert.equal(result.hover?.kind, kind);
      assert.deepStrictEqual(result.hover?.declaration,
        candidate?.declaration);
      assert.equal(symbol(
        result, "INCOMPLETE_HEADER_DERIVED", "enumConstant",
      )?.initializer.constantValue, value);
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--incomplete-enum-header-revision-parity-json",
          String(revisionIndex + 1), String(sourceIndex), String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm incomplete enum header kind differ for ${cursorName} at revision ${revisionIndex + 1}`,
    );
  }
} finally {
  incompleteEnumHeaderKindCompiler.dispose();
}

const incompleteEnumHeaderMacroShapeCases = [
  [0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", false, true, "19",
    "header enum macro documentation",
    "/// header enum macro documentation"],
  [0, 3, "INCOMPLETE_HEADER_ENUM_MA", false, false, "19",
    "header enum macro documentation",
    "/// header enum macro documentation"],
  [5, 1, "INCOMPLETE_HEADER_ENUM_MACRO", true, false, "61",
    "function-like header macro documentation",
    "/// function-like header macro documentation"],
  [5, 3, "INCOMPLETE_HEADER_ENUM_MA", true, false, "61",
    "function-like header macro documentation",
    "/// function-like header macro documentation"],
  [0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", false, true, "19",
    "header enum macro documentation",
    "/// header enum macro documentation"],
  [0, 3, "INCOMPLETE_HEADER_ENUM_MA", false, false, "19",
    "header enum macro documentation",
    "/// header enum macro documentation"],
];
const incompleteEnumHeaderMacroShapeCompiler = await createCompiler(
  wasmModule,
);
try {
  for (const [revisionIndex, sourceIndex, cursorName, functionLike, resolves,
    replacement, documentation, commentText] of
    incompleteEnumHeaderMacroShapeCases) {
    const revision = incompleteEnumHeaderRevisions[revisionIndex];
    const source = incompleteEnumHeaderSources[sourceIndex];
    const candidateName = "INCOMPLETE_HEADER_ENUM_MACRO";
    const cursorIndex = source.source.lastIndexOf(cursorName);
    const cursorStart = byteOffsetForIndex(source.source, cursorIndex);
    const byteOffset = cursorStart +
      Math.floor(Buffer.byteLength(cursorName) / 2);
    const declarationIndex = revision.header.indexOf(candidateName);
    const commentIndex = revision.header.indexOf(commentText);
    assert.ok(cursorIndex >= 0 && declarationIndex >= 0 && commentIndex >= 0,
      `${cursorName} incomplete enum header macro shape anchors`);
    const result = incompleteEnumHeaderMacroShapeCompiler.analyzeSource(
      source,
      {
        headers: { "incomplete-enum.h": revision.header },
        cursor: { sourceName: source.name, byteOffset },
      },
    );
    const candidate = symbol(result, candidateName, "macro");
    assert.equal(result.partial, true);
    assert.ok(candidate);
    assert.equal(candidate?.declaration.sourceName, "incomplete-enum.h");
    assert.equal(candidate?.declaration.start.offset,
      byteOffsetForIndex(revision.header, declarationIndex));
    assert.equal(candidate?.declaration.end.offset,
      byteOffsetForIndex(revision.header, declarationIndex) +
        Buffer.byteLength(candidateName));
    assert.equal(candidate?.documentation, documentation);
    assert.equal(candidate?.documentationRange?.sourceName,
      "incomplete-enum.h");
    assert.equal(candidate?.documentationRange?.start.offset,
      byteOffsetForIndex(revision.header, commentIndex));
    assert.equal(candidate?.documentationRange?.end.offset,
      byteOffsetForIndex(revision.header, commentIndex) +
        Buffer.byteLength(commentText));
    assert.equal(candidate?.macro?.functionLike, functionLike);
    assert.equal(candidate?.macro?.variadic, false);
    assert.deepStrictEqual(candidate?.macro?.parameters,
      functionLike ? ["left", "right"] : []);
    assert.equal(candidate?.macro?.replacement, replacement);
    assert.equal(candidate?.initializer.constantValue, null);
    assert.deepStrictEqual(result.dependencies, ["incomplete-enum.h"]);
    if (resolves) {
      assert.deepStrictEqual(result.diagnostics, []);
      assert.equal(result.hover?.name, candidateName);
      assert.deepStrictEqual(result.hover?.declaration,
        candidate?.declaration);
      assert.equal(symbol(
        result, "INCOMPLETE_HEADER_DERIVED", "enumConstant",
      )?.initializer.constantValue, replacement);
    } else {
      assert.equal(result.hover, null);
      assert.equal(result.diagnostics.length, 1);
      assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
      assert.equal(result.diagnostics[0].start.offset, cursorStart);
      assert.equal(result.diagnostics[0].end.offset,
        cursorStart + Buffer.byteLength(cursorName));
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--incomplete-enum-header-revision-parity-json",
          String(revisionIndex + 1), String(sourceIndex), String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm incomplete enum header macro shape differ for ${cursorName} at revision ${revisionIndex + 1}`,
    );
  }
} finally {
  incompleteEnumHeaderMacroShapeCompiler.dispose();
}

const incompleteEnumHeaderCollisionCases = [
  [6, "enumConstant", "73"],
  [7, "macro", "71"],
];
const incompleteEnumHeaderCollisionName = "COLLIDING_HEADER_SYMBOL";
const incompleteEnumHeaderCollisionRevision =
  incompleteEnumHeaderRevisions[6];
const incompleteEnumHeaderCollisionMacroDeclaration =
  incompleteEnumHeaderCollisionRevision.header.indexOf(
    incompleteEnumHeaderCollisionName,
  );
const incompleteEnumHeaderCollisionEnumDeclaration =
  incompleteEnumHeaderCollisionRevision.header.lastIndexOf(
    incompleteEnumHeaderCollisionName,
  );
const incompleteEnumHeaderCollisionMacroComment =
  incompleteEnumHeaderCollisionRevision.header.indexOf(
    "/// colliding header macro documentation",
  );
const incompleteEnumHeaderCollisionEnumComment =
  incompleteEnumHeaderCollisionRevision.header.indexOf(
    "/// colliding header enum documentation",
  );
assert.ok(incompleteEnumHeaderCollisionMacroDeclaration >= 0 &&
  incompleteEnumHeaderCollisionEnumDeclaration >= 0 &&
  incompleteEnumHeaderCollisionMacroDeclaration !==
    incompleteEnumHeaderCollisionEnumDeclaration &&
  incompleteEnumHeaderCollisionMacroComment >= 0 &&
  incompleteEnumHeaderCollisionEnumComment >= 0,
"incomplete enum header namespace collision anchors");
const freshIncompleteEnumHeaderCollisionCompiler = await createCompiler(
  wasmModule,
);
try {
  for (const collisionCompiler of [
    compiler, freshIncompleteEnumHeaderCollisionCompiler,
  ]) {
    for (const [sourceIndex, hoverKind, derivedValue] of
      incompleteEnumHeaderCollisionCases) {
      const source = incompleteEnumHeaderSources[sourceIndex];
      const useIndex = source.source.lastIndexOf(
        incompleteEnumHeaderCollisionName,
      );
      const useStart = byteOffsetForIndex(source.source, useIndex);
      const nameLength = Buffer.byteLength(incompleteEnumHeaderCollisionName);
      for (const delta of [0, Math.floor(nameLength / 2), nameLength]) {
        const byteOffset = useStart + delta;
        const result = collisionCompiler.analyzeSource(source, {
          headers: {
            "incomplete-enum.h": incompleteEnumHeaderCollisionRevision.header,
          },
          cursor: { sourceName: source.name, byteOffset },
        });
        const enumCandidate = symbol(
          result, incompleteEnumHeaderCollisionName, "enumConstant",
        );
        const macroCandidate = symbol(
          result, incompleteEnumHeaderCollisionName, "macro",
        );
        assert.equal(result.partial, true);
        assert.deepStrictEqual(result.diagnostics, []);
        assert.equal(result.hover?.name, incompleteEnumHeaderCollisionName);
        assert.equal(result.hover?.kind, hoverKind);
        assert.ok(enumCandidate && macroCandidate);
        assert.deepStrictEqual(
          result.hover?.declaration,
          hoverKind === "enumConstant"
            ? enumCandidate?.declaration : macroCandidate?.declaration,
        );
        assert.equal(enumCandidate?.initializer.constantValue, "73");
        assert.equal(enumCandidate?.declaration.start.offset,
          incompleteEnumHeaderCollisionEnumDeclaration);
        assert.equal(enumCandidate?.declaration.end.offset,
          incompleteEnumHeaderCollisionEnumDeclaration + nameLength);
        assert.equal(enumCandidate?.documentation,
          "colliding header enum documentation");
        assert.equal(enumCandidate?.documentationRange?.sourceName,
          "incomplete-enum.h");
        assert.equal(enumCandidate?.documentationRange?.start.offset,
          incompleteEnumHeaderCollisionEnumComment);
        assert.equal(enumCandidate?.documentationRange?.end.offset,
          incompleteEnumHeaderCollisionEnumComment +
            Buffer.byteLength("/// colliding header enum documentation"));
        assert.equal(macroCandidate?.macro?.functionLike, true);
        assert.equal(macroCandidate?.macro?.variadic, false);
        assert.deepStrictEqual(macroCandidate?.macro?.parameters, ["value"]);
        assert.equal(macroCandidate?.macro?.replacement,
          "( ( value ) + 70 )");
        assert.equal(macroCandidate?.declaration.start.offset,
          incompleteEnumHeaderCollisionMacroDeclaration);
        assert.equal(macroCandidate?.declaration.end.offset,
          incompleteEnumHeaderCollisionMacroDeclaration + nameLength);
        assert.equal(macroCandidate?.documentation,
          "colliding header macro documentation");
        assert.equal(macroCandidate?.documentationRange?.sourceName,
          "incomplete-enum.h");
        assert.equal(macroCandidate?.documentationRange?.start.offset,
          incompleteEnumHeaderCollisionMacroComment);
        assert.equal(macroCandidate?.documentationRange?.end.offset,
          incompleteEnumHeaderCollisionMacroComment +
            Buffer.byteLength("/// colliding header macro documentation"));
        assert.equal(symbol(
          result, "INCOMPLETE_HEADER_DERIVED", "enumConstant",
        )?.initializer.constantValue, derivedValue);
        assert.deepStrictEqual(result.dependencies, ["incomplete-enum.h"]);
        assert.deepStrictEqual(
          result,
          JSON.parse(execFileSync(
            nativeAnalysisPath,
            ["--incomplete-enum-header-revision-parity-json", "7",
              String(sourceIndex), String(byteOffset)],
            { encoding: "utf8" },
          )),
          `native and Wasm incomplete enum header namespace collision differ for ${hoverKind} at ${delta}`,
        );
      }
    }
  }
} finally {
  freshIncompleteEnumHeaderCollisionCompiler.dispose();
}

const incompleteEnumHeaderCollisionRevisions = [
  {
    revisionIndex: 6,
    enumValue: "73",
    enumDocumentation: "colliding header enum documentation",
    enumComment: "/// colliding header enum documentation",
    macroReplacement: "( ( value ) + 70 )",
    macroInvocationValue: "71",
    macroDocumentation: "colliding header macro documentation",
    macroComment: "/// colliding header macro documentation",
  },
  {
    revisionIndex: 7,
    enumValue: "83",
    enumDocumentation: "enum-only colliding header documentation",
    enumComment: "/// enum-only colliding header documentation",
    macroReplacement: null,
    macroInvocationValue: null,
    macroDocumentation: null,
    macroComment: null,
  },
  {
    revisionIndex: 8,
    enumValue: null,
    enumDocumentation: null,
    enumComment: null,
    macroReplacement: "( ( value ) + 80 )",
    macroInvocationValue: "81",
    macroDocumentation: "macro-only colliding header documentation",
    macroComment: "/// macro-only colliding header documentation",
  },
  {
    revisionIndex: 6,
    enumValue: "73",
    enumDocumentation: "colliding header enum documentation",
    enumComment: "/// colliding header enum documentation",
    macroReplacement: "( ( value ) + 70 )",
    macroInvocationValue: "71",
    macroDocumentation: "colliding header macro documentation",
    macroComment: "/// colliding header macro documentation",
  },
];
const incompleteEnumHeaderCollisionRevisionCompiler = await createCompiler(
  wasmModule,
);
try {
  for (const revision of incompleteEnumHeaderCollisionRevisions) {
    const header = incompleteEnumHeaderRevisions[revision.revisionIndex].header;
    for (const [sourceIndex] of incompleteEnumHeaderCollisionCases) {
      const source = incompleteEnumHeaderSources[sourceIndex];
      const invocation = sourceIndex === 7;
      const useIndex = source.source.lastIndexOf(
        incompleteEnumHeaderCollisionName,
      );
      const useStart = byteOffsetForIndex(source.source, useIndex);
      const nameLength = Buffer.byteLength(incompleteEnumHeaderCollisionName);
      for (const delta of [0, Math.floor(nameLength / 2), nameLength]) {
        const byteOffset = useStart + delta;
        const result = incompleteEnumHeaderCollisionRevisionCompiler
          .analyzeSource(source, {
            headers: { "incomplete-enum.h": header },
            cursor: { sourceName: source.name, byteOffset },
          });
        const enumCandidate = symbol(
          result, incompleteEnumHeaderCollisionName, "enumConstant",
        );
        const macroCandidate = symbol(
          result, incompleteEnumHeaderCollisionName, "macro",
        );
        assert.equal(result.partial, true);
        assert.equal(Boolean(enumCandidate), revision.enumValue !== null);
        assert.equal(Boolean(macroCandidate),
          revision.macroReplacement !== null);
        assert.deepStrictEqual(result.dependencies, ["incomplete-enum.h"]);
        if (enumCandidate) {
          const declarationIndex = header.lastIndexOf(
            incompleteEnumHeaderCollisionName,
          );
          const commentIndex = header.indexOf(revision.enumComment);
          assert.equal(enumCandidate.initializer.constantValue,
            revision.enumValue);
          assert.equal(enumCandidate.declaration.start.offset,
            declarationIndex);
          assert.equal(enumCandidate.declaration.end.offset,
            declarationIndex + nameLength);
          assert.equal(enumCandidate.documentation,
            revision.enumDocumentation);
          assert.equal(enumCandidate.documentationRange?.start.offset,
            commentIndex);
          assert.equal(enumCandidate.documentationRange?.sourceName,
            "incomplete-enum.h");
          assert.equal(enumCandidate.documentationRange?.end.offset,
            commentIndex + Buffer.byteLength(revision.enumComment));
        }
        if (macroCandidate) {
          const declarationIndex = header.indexOf(
            incompleteEnumHeaderCollisionName,
          );
          const commentIndex = header.indexOf(revision.macroComment);
          assert.equal(macroCandidate.macro?.functionLike, true);
          assert.equal(macroCandidate.macro?.variadic, false);
          assert.deepStrictEqual(macroCandidate.macro?.parameters, ["value"]);
          assert.equal(macroCandidate.macro?.replacement,
            revision.macroReplacement);
          assert.equal(macroCandidate.declaration.start.offset,
            declarationIndex);
          assert.equal(macroCandidate.declaration.end.offset,
            declarationIndex + nameLength);
          assert.equal(macroCandidate.documentation,
            revision.macroDocumentation);
          assert.equal(macroCandidate.documentationRange?.start.offset,
            commentIndex);
          assert.equal(macroCandidate.documentationRange?.sourceName,
            "incomplete-enum.h");
          assert.equal(macroCandidate.documentationRange?.end.offset,
            commentIndex + Buffer.byteLength(revision.macroComment));
        }
        const derived = symbol(
          result, "INCOMPLETE_HEADER_DERIVED", "enumConstant",
        );
        if (!revision.enumValue && !invocation) {
          assert.equal(result.hover, null);
          assert.equal(result.diagnostics.length, 1);
          assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
          assert.equal(result.diagnostics[0].start.offset, useStart);
          assert.equal(result.diagnostics[0].end.offset,
            useStart + nameLength);
        } else {
          const expected = invocation && macroCandidate
            ? macroCandidate : enumCandidate;
          const invalidEnumInvocation = invocation && !macroCandidate;
          const expectedDiagnosticCount = invalidEnumInvocation &&
              delta === nameLength ? 1 : 0;
          assert.equal(result.diagnostics.length, expectedDiagnosticCount);
          if (expectedDiagnosticCount) {
            assert.equal(result.diagnostics[0].code, "E3102");
            assert.equal(result.diagnostics[0].start.offset,
              useStart + nameLength);
            assert.equal(result.diagnostics[0].end.offset,
              useStart + nameLength + 1);
          }
          assert.equal(result.hover?.kind, expected?.kind);
          assert.deepStrictEqual(result.hover?.declaration,
            expected?.declaration);
          if (invalidEnumInvocation) {
            assert.equal(derived, undefined);
          } else {
            assert.equal(derived?.initializer.constantValue,
              invocation
                ? revision.macroInvocationValue : revision.enumValue);
          }
        }
        assert.deepStrictEqual(
          result,
          JSON.parse(execFileSync(
            nativeAnalysisPath,
            ["--incomplete-enum-header-revision-parity-json",
              String(revision.revisionIndex + 1), String(sourceIndex),
              String(byteOffset)],
            { encoding: "utf8" },
          )),
          `native and Wasm incomplete enum header namespace revision differ for ${sourceIndex} at revision ${revision.revisionIndex + 1} and ${delta}`,
        );
      }
    }
  }
} finally {
  incompleteEnumHeaderCollisionRevisionCompiler.dispose();
}

const projectEnumMacroRevisionCompiler = await createCompiler(wasmModule);
const projectEnumMacroName = "PROJECT_COLLIDING_SYMBOL";
const projectEnumMacroSpacedRevisionIndices = new Set([0, 3, 7, 9, 11, 15]);
const projectEnumMacroIdentifierArgumentRevisionIndices =
  new Set([0, 3, 7, 15]);
try {
  for (let revisionIndex = 0;
    revisionIndex < projectEnumMacroRevisions.length;
    revisionIndex++) {
    const revision = projectEnumMacroRevisions[revisionIndex];
    const header = projectEnumMacroHeaders[revision.headerIndex];
    const firstSourceModeResults = new Map();
    const sourceModes = projectEnumMacroIdentifierArgumentRevisionIndices.has(
      revisionIndex,
    ) ? [0, 1, 2, 3, 4, 1, 2, 3, 4, 0]
      : projectEnumMacroSpacedRevisionIndices.has(revisionIndex)
        ? [0, 1, 2, 1, 2, 0] : [0, 1, 0, 1];
    for (const sourceMode of sourceModes) {
      const invocation = sourceMode !== 0;
      if (invocation && !revision.enumValue && !revision.macroReplacement) {
        continue;
      }
      let source = projectEnumMacroEditSources[
        revision.sourceIndex][invocation ? 1 : 0];
      if (sourceMode === 2) {
        source = projectEnumMacroSpacedCallSource(source);
      } else if (sourceMode === 3) {
        source = projectEnumMacroIdentifierArgumentSource(
          source,
          "enum {\n" +
            "  /// project call enum argument\n" +
            "  PROJECT_CALL_ENUM_ARGUMENT = 1\n" +
            "};\n",
          "  PROJECT_CALL_ENUM_ARGUMENT  ",
        );
      } else if (sourceMode === 4) {
        source = projectEnumMacroIdentifierArgumentSource(
          source,
          "/// project call macro argument\n" +
            "#define PROJECT_CALL_MACRO_ARGUMENT 1\n",
          "  PROJECT_CALL_MACRO_ARGUMENT  ",
        );
      }
      const useIndex = source.source.lastIndexOf(projectEnumMacroName);
      const useStart = byteOffsetForIndex(source.source, useIndex);
      const nameLength = Buffer.byteLength(projectEnumMacroName);
      const nameEnd = useStart + nameLength;
      const callOpenIndex = invocation
        ? source.source.indexOf("(", useIndex + projectEnumMacroName.length)
        : -1;
      const argumentName = sourceMode === 3
        ? "PROJECT_CALL_ENUM_ARGUMENT"
        : sourceMode === 4 ? "PROJECT_CALL_MACRO_ARGUMENT" : "1";
      const argumentIndex = callOpenIndex >= 0
        ? source.source.indexOf(argumentName, callOpenIndex + 1) : -1;
      const callCloseIndex = argumentIndex >= 0
        ? source.source.indexOf(")", argumentIndex + argumentName.length)
        : -1;
      assert.ok(!invocation ||
        (callOpenIndex >= 0 && argumentIndex >= 0 && callCloseIndex >= 0),
      "project enum/macro invocation anchors");
      const callOpen = callOpenIndex >= 0
        ? byteOffsetForIndex(source.source, callOpenIndex) : -1;
      const argumentEnd = argumentIndex >= 0
        ? byteOffsetForIndex(
          source.source, argumentIndex + argumentName.length,
        ) : -1;
      const callEnd = callCloseIndex >= 0
        ? byteOffsetForIndex(source.source, callCloseIndex + 1) : -1;
      const cursorSteps = [
        { byteOffset: useStart - 1, outside: true, label: "outside" },
        { byteOffset: useStart, outside: false, label: "start" },
        {
          byteOffset: useStart + Math.floor(nameLength / 2),
          outside: false,
          label: "middle",
        },
        { byteOffset: nameEnd, outside: false, label: "name-end" },
        { byteOffset: useStart - 1, outside: true, label: "outside" },
      ];
      if (invocation) {
        cursorSteps.push(
          { byteOffset: nameEnd, outside: false, label: "name-end" },
          { byteOffset: callOpen + 1, outside: false, label: "call-open" },
        );
        if (sourceMode === 3 || sourceMode === 4) {
          const argumentStart = byteOffsetForIndex(
            source.source, argumentIndex,
          );
          cursorSteps.push(
            {
              byteOffset: argumentStart,
              outside: false,
              label: "argument-start",
            },
            {
              byteOffset: argumentStart +
                Math.floor(Buffer.byteLength(argumentName) / 2),
              outside: false,
              label: "argument-middle",
            },
            {
              byteOffset: argumentEnd,
              outside: false,
              label: "argument-end",
            },
            {
              byteOffset: argumentEnd + 1,
              outside: false,
              label: "argument-after",
            },
          );
        } else {
          cursorSteps.push({
            byteOffset: argumentEnd,
            outside: false,
            label: "argument-end",
          });
        }
        cursorSteps.push(
          { byteOffset: callEnd, outside: false, label: "call-end" },
          { byteOffset: nameEnd, outside: false, label: "name-end" },
        );
      }
      for (const { byteOffset, outside, label } of cursorSteps) {
        const result = projectEnumMacroRevisionCompiler.analyzeProjectSource(
          source,
          {
            projectRevision: revisionIndex + 1,
            projectSources: [
              projectEnumMacroIndexSources[revision.sourceIndex],
            ],
            headers: { "project-collision.h": header },
            cursor: { sourceName: source.name, byteOffset },
          },
        );
        const enumCandidate = symbol(
          result, projectEnumMacroName, "enumConstant",
        );
        const macroCandidate = symbol(
          result, projectEnumMacroName, "macro",
        );
        const renamedEnumName = "PROJECT_RENAMED_SYMBOL";
        const renamedEnumCandidate = symbol(
          result, renamedEnumName, "enumConstant",
        );
        const argumentEnumName = "PROJECT_CALL_ENUM_ARGUMENT";
        const argumentMacroName = "PROJECT_CALL_MACRO_ARGUMENT";
        const argumentEnumCandidate = symbol(
          result, argumentEnumName, "enumConstant",
        );
        const argumentMacroCandidate = symbol(
          result, argumentMacroName, "macro",
        );
        assert.equal(result.partial, true);
        assert.equal(Boolean(enumCandidate), revision.enumValue !== null);
        assert.equal(Boolean(macroCandidate),
          revision.macroReplacement !== null);
        assert.equal(Boolean(renamedEnumCandidate),
          revision.renamedEnumValue !== undefined &&
            revision.renamedEnumValue !== null);
        assert.equal(Boolean(argumentEnumCandidate), sourceMode === 3);
        assert.equal(Boolean(argumentMacroCandidate), sourceMode === 4);
        assert.deepStrictEqual(result.dependencies, ["project-collision.h"]);
        if (enumCandidate) {
          const declarationIndex = source.source.indexOf(projectEnumMacroName);
          const commentIndex = source.source.indexOf(revision.enumComment);
          assert.equal(enumCandidate.initializer.constantValue,
            revision.enumValue);
          assert.equal(enumCandidate.declaration.sourceName, "main.c");
          assert.equal(enumCandidate.declaration.start.offset,
            declarationIndex);
          assert.equal(enumCandidate.declaration.end.offset,
            declarationIndex + nameLength);
          assert.equal(enumCandidate.documentation,
            revision.enumDocumentation);
          assert.equal(enumCandidate.documentationRange?.sourceName, "main.c");
          assert.equal(enumCandidate.documentationRange?.start.offset,
            commentIndex);
          assert.equal(enumCandidate.documentationRange?.end.offset,
            commentIndex + Buffer.byteLength(revision.enumComment));
        }
        if (renamedEnumCandidate) {
          const declarationIndex = source.source.indexOf(renamedEnumName);
          const commentIndex = source.source.indexOf(
            revision.renamedEnumComment,
          );
          assert.equal(renamedEnumCandidate.initializer.constantValue,
            revision.renamedEnumValue);
          assert.equal(renamedEnumCandidate.declaration.sourceName, "main.c");
          assert.equal(renamedEnumCandidate.declaration.start.offset,
            declarationIndex);
          assert.equal(renamedEnumCandidate.declaration.end.offset,
            declarationIndex + Buffer.byteLength(renamedEnumName));
          assert.equal(renamedEnumCandidate.documentation,
            revision.renamedEnumDocumentation);
          assert.equal(renamedEnumCandidate.documentationRange?.sourceName,
            "main.c");
          assert.equal(renamedEnumCandidate.documentationRange?.start.offset,
            commentIndex);
          assert.equal(renamedEnumCandidate.documentationRange?.end.offset,
            commentIndex + Buffer.byteLength(revision.renamedEnumComment));
        }
        if (argumentEnumCandidate) {
          const declarationIndex = source.source.indexOf(argumentEnumName);
          const comment = "/// project call enum argument";
          const commentIndex = source.source.indexOf(comment);
          assert.equal(argumentEnumCandidate.initializer.constantValue, "1");
          assert.equal(argumentEnumCandidate.declaration.sourceName, "main.c");
          assert.equal(argumentEnumCandidate.declaration.start.offset,
            declarationIndex);
          assert.equal(argumentEnumCandidate.declaration.end.offset,
            declarationIndex + Buffer.byteLength(argumentEnumName));
          assert.equal(argumentEnumCandidate.documentation,
            "project call enum argument");
          assert.equal(argumentEnumCandidate.documentationRange?.sourceName,
            "main.c");
          assert.equal(argumentEnumCandidate.documentationRange?.start.offset,
            commentIndex);
          assert.equal(argumentEnumCandidate.documentationRange?.end.offset,
            commentIndex + Buffer.byteLength(comment));
        }
        if (argumentMacroCandidate) {
          const declarationIndex = source.source.indexOf(argumentMacroName);
          const comment = "/// project call macro argument";
          const commentIndex = source.source.indexOf(comment);
          assert.equal(argumentMacroCandidate.macro?.functionLike, false);
          assert.equal(argumentMacroCandidate.macro?.replacement, "1");
          assert.equal(argumentMacroCandidate.declaration.sourceName, "main.c");
          assert.equal(argumentMacroCandidate.declaration.start.offset,
            declarationIndex);
          assert.equal(argumentMacroCandidate.declaration.end.offset,
            declarationIndex + Buffer.byteLength(argumentMacroName));
          assert.equal(argumentMacroCandidate.documentation,
            "project call macro argument");
          assert.equal(argumentMacroCandidate.documentationRange?.sourceName,
            "main.c");
          assert.equal(argumentMacroCandidate.documentationRange?.start.offset,
            commentIndex);
          assert.equal(argumentMacroCandidate.documentationRange?.end.offset,
            commentIndex + Buffer.byteLength(comment));
        }
        if (macroCandidate) {
          const declarationIndex = header.indexOf(projectEnumMacroName);
          const commentIndex = header.indexOf(revision.macroComment);
          assert.equal(macroCandidate.macro?.functionLike, true);
          assert.equal(macroCandidate.macro?.variadic, false);
          assert.deepStrictEqual(macroCandidate.macro?.parameters,
            [revision.macroParameter ?? "value"]);
          assert.equal(macroCandidate.macro?.replacement,
            revision.macroReplacement);
          assert.equal(macroCandidate.declaration.sourceName,
            "project-collision.h");
          assert.equal(macroCandidate.declaration.start.offset,
            declarationIndex);
          assert.equal(macroCandidate.declaration.end.offset,
            declarationIndex + nameLength);
          assert.equal(macroCandidate.documentation,
            revision.macroDocumentation);
          assert.equal(macroCandidate.documentationRange?.sourceName,
            "project-collision.h");
          assert.equal(macroCandidate.documentationRange?.start.offset,
            commentIndex);
          assert.equal(macroCandidate.documentationRange?.end.offset,
            commentIndex + Buffer.byteLength(revision.macroComment));
        }
        const derived = symbol(
          result, "PROJECT_COLLISION_DERIVED", "enumConstant",
        );
        if (outside) {
          assert.equal(result.hover, null);
          assert.equal(result.diagnostics.length, 0);
          assert.equal(derived, undefined);
        } else if (!invocation && !revision.enumValue) {
          assert.equal(result.hover, null);
          assert.equal(result.diagnostics.length, 1);
          assert.equal(result.diagnostics[0].code, "AGC_PARTIAL_IDENTIFIER");
          assert.equal(result.diagnostics[0].start.offset, useStart);
          assert.equal(result.diagnostics[0].end.offset,
            useStart + nameLength);
        } else {
          const expected = invocation && macroCandidate
            ? macroCandidate : enumCandidate;
          const invalidEnumInvocation = invocation && !macroCandidate;
          const cursorAfterName = invocation && byteOffset > nameEnd;
          const argumentStart = argumentIndex >= 0
            ? byteOffsetForIndex(source.source, argumentIndex) : -1;
          const cursorInArgument =
            (sourceMode === 3 || sourceMode === 4) &&
            byteOffset >= argumentStart && byteOffset <= argumentEnd;
          const expectedArgument = sourceMode === 3
            ? argumentEnumCandidate : argumentMacroCandidate;
          const expectedDiagnosticCount = invalidEnumInvocation &&
              byteOffset >= callOpen ? 1 : 0;
          assert.equal(result.diagnostics.length, expectedDiagnosticCount);
          if (expectedDiagnosticCount) {
            assert.equal(result.diagnostics[0].code, "E3102");
            assert.equal(result.diagnostics[0].start.offset,
              callOpen);
            assert.equal(result.diagnostics[0].end.offset,
              callOpen + 1);
          }
          if (cursorInArgument) {
            assert.equal(result.hover?.kind, expectedArgument?.kind);
            assert.deepStrictEqual(result.hover?.declaration,
              expectedArgument?.declaration);
          } else if (cursorAfterName) {
            assert.equal(result.hover, null);
          } else {
            assert.equal(result.hover?.kind, expected?.kind);
            assert.deepStrictEqual(result.hover?.declaration,
              expected?.declaration);
          }
          if (invalidEnumInvocation) {
            assert.equal(derived, undefined);
          } else {
            assert.equal(derived?.initializer.constantValue,
              invocation
                ? revision.macroInvocationValue : revision.enumValue);
          }
        }
        const sourceModeKey = `${sourceMode}:${byteOffset}`;
        const firstSourceModeResult = firstSourceModeResults.get(sourceModeKey);
        if (firstSourceModeResult) {
          assert.deepStrictEqual(
            result,
            firstSourceModeResult,
            `Wasm project enum/macro cursor/source toggle retained stale state for ${revisionIndex + 1}, ${sourceMode}, ${label}`,
          );
        } else {
          firstSourceModeResults.set(sourceModeKey, result);
          assert.deepStrictEqual(
            result,
            JSON.parse(execFileSync(
              nativeAnalysisPath,
              ["--project-enum-macro-revision-parity-json",
                String(revisionIndex + 1), String(sourceMode),
                String(byteOffset)],
              { encoding: "utf8" },
            )),
            `native and Wasm project enum/macro revision differ for ${revisionIndex + 1}, ${sourceMode}, ${label}`,
          );
        }
      }
    }
  }
} finally {
  projectEnumMacroRevisionCompiler.dispose();
}

const enumTwoArgumentCompiler = await createCompiler(wasmModule);
const enumTwoArgumentNames = [
  "ENUM_TWO_ARGUMENT_CALL",
  "ENUM_TWO_ARGUMENT_FIRST",
  "ENUM_TWO_ARGUMENT_SECOND",
];
const enumTwoArgumentValues = ["7", "1", "2"];
const enumTwoArgumentDocumentation = [
  "enum two argument callee",
  "enum two argument first",
  "enum two argument second",
];
const enumTwoArgumentComments = [
  "/// enum two argument callee",
  "/// enum two argument first",
  "/// enum two argument second",
];
const enumTwoArgumentMacroNames = [
  [null, "ENUM_TWO_ARGUMENT_FIRST_MACRO",
    "ENUM_TWO_ARGUMENT_SECOND_MACRO"],
  [null, "ENUM_TWO_ARGUMENT_FIRST_MACRO",
    "ENUM_TWO_ARGUMENT_SECOND_MACRO"],
  [null, "ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO",
    "ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO"],
  [null, "ENUM_TWO_ARGUMENT_FIRST_MACRO",
    "ENUM_TWO_ARGUMENT_SECOND_MACRO"],
];
const enumTwoArgumentMacroValues = [
  [null, "1", "2"], [null, "11", "12"],
  [null, "1", "2"],
  [null, null, null],
];
const enumTwoArgumentMacroDocumentation = [
  [null, "enum two argument first macro",
    "enum two argument second macro"],
  [null, "enum two argument first macro updated",
    "enum two argument second macro updated"],
  [null, "enum two argument first renamed macro",
    "enum two argument second renamed macro"],
  [null, null, null],
];
const enumTwoArgumentMacroComments = [
  [null, "/// enum two argument first macro",
    "/// enum two argument second macro"],
  [null, "/// enum two argument first macro updated",
    "/// enum two argument second macro updated"],
  [null, "/// enum two argument first renamed macro",
    "/// enum two argument second renamed macro"],
  [null, null, null],
];
const enumTwoArgumentFirstResults = new Map();
try {
  for (const [variant, state, argumentMode, argumentRevision] of [
    [0, 0, 0, 0], [1, 0, 0, 0], [2, 0, 0, 0], [3, 0, 0, 0],
    [0, 1, 0, 0], [1, 1, 0, 0], [2, 1, 0, 0], [3, 1, 0, 0],
    [1, 0, 1, 0], [2, 0, 1, 1], [3, 0, 1, 0],
    [1, 0, 1, 1], [2, 0, 1, 0], [3, 0, 1, 1], [1, 0, 1, 0],
    [3, 0, 2, 0], [2, 0, 2, 1], [1, 0, 2, 0],
    [3, 0, 2, 1], [2, 0, 2, 0], [1, 0, 2, 1], [3, 0, 2, 0],
    [1, 1, 1, 1], [3, 1, 2, 1],
    [2, 0, 1, 2], [3, 0, 1, 0], [1, 0, 1, 2],
    [2, 0, 1, 0], [3, 0, 1, 2], [1, 0, 1, 0],
    [2, 0, 2, 2], [1, 0, 2, 0], [3, 0, 2, 2],
    [2, 0, 2, 0], [1, 0, 2, 2], [3, 0, 2, 0],
    [1, 1, 1, 2], [3, 1, 2, 2],
    [1, 0, 1, 3], [2, 0, 1, 0],
    [3, 0, 1, 3], [1, 0, 1, 0],
    [2, 0, 2, 3], [1, 0, 2, 0],
    [3, 0, 2, 3], [2, 0, 2, 0],
    [1, 1, 1, 3], [3, 1, 2, 3],
    [1, 0, 3, 0], [2, 0, 3, 1],
    [3, 0, 3, 0], [1, 0, 3, 2],
    [2, 0, 3, 0], [3, 0, 3, 1],
    [1, 0, 3, 0], [1, 1, 3, 0],
    [3, 1, 3, 2],
    [2, 0, 3, 3], [3, 0, 3, 0],
    [1, 0, 3, 3], [2, 0, 3, 0],
    [2, 1, 3, 3],
    [1, 0, 3, 3], [1, 0, 3, 2], [1, 0, 3, 0],
    [3, 0, 3, 3], [3, 0, 3, 1], [3, 0, 3, 0],
    [1, 0, 3, 0], [1, 0, 3, 1], [1, 0, 3, 3],
    [1, 0, 3, 2], [1, 0, 3, 0],
    [2, 0, 3, 0], [2, 0, 3, 2], [2, 0, 3, 3],
    [2, 0, 3, 1], [2, 0, 3, 0],
    [1, 0, 3, 0], [1, 0, 4, 0], [1, 0, 4, 1],
    [1, 0, 4, 0], [1, 0, 3, 0], [1, 1, 4, 1],
    [3, 0, 3, 0], [3, 0, 5, 0], [3, 0, 5, 1],
    [3, 0, 5, 0], [3, 0, 3, 0], [3, 1, 5, 1],
    [1, 0, 3, 0], [1, 0, 6, 0], [1, 0, 6, 1],
    [1, 0, 6, 0], [1, 0, 3, 0], [1, 1, 6, 1],
    [3, 0, 3, 0], [3, 0, 7, 0], [3, 0, 7, 1],
    [3, 0, 7, 0], [3, 0, 3, 0], [3, 1, 7, 1],
    [1, 0, 3, 0], [1, 0, 8, 0], [1, 0, 8, 1],
    [1, 0, 8, 0], [1, 0, 3, 0], [1, 1, 8, 1],
    [3, 0, 3, 0], [3, 0, 9, 0], [3, 0, 9, 1],
    [3, 0, 9, 0], [3, 0, 3, 0], [3, 1, 9, 1],
    [1, 0, 3, 0], [1, 0, 10, 0], [1, 0, 10, 1],
    [1, 0, 10, 0], [1, 0, 10, 2], [1, 0, 10, 0],
    [1, 0, 3, 0], [1, 1, 10, 1],
    [3, 0, 3, 0], [3, 0, 10, 0], [3, 0, 10, 2],
    [3, 0, 10, 0], [3, 0, 10, 1], [3, 0, 10, 0],
    [3, 0, 3, 0], [3, 1, 10, 2],
    [1, 0, 10, 0], [1, 0, 10, 3], [1, 0, 10, 0],
    [1, 1, 10, 3],
    [3, 0, 10, 0], [3, 0, 10, 3], [3, 0, 10, 0],
    [3, 1, 10, 3],
    [1, 0, 10, 3], [1, 0, 10, 2], [1, 0, 10, 0],
    [3, 0, 10, 3], [3, 0, 10, 1], [3, 0, 10, 0],
    [1, 0, 10, 0], [1, 0, 10, 1], [1, 0, 10, 3],
    [1, 0, 10, 2], [1, 0, 10, 0],
    [3, 0, 10, 0], [3, 0, 10, 2], [3, 0, 10, 3],
    [3, 0, 10, 1], [3, 0, 10, 0],
    [0, 0, 0, 0], [3, 1, 0, 0],
  ]) {
    const source = argumentMode === 10
      ? enumTwoArgumentBothRenamedSource(
        enumTwoArgumentCallSources[variant], argumentRevision,
      )
      : argumentMode >= 8
      ? enumTwoArgumentPairedRenameUpdateSource(
        enumTwoArgumentCallSources[variant], argumentMode - 7,
        argumentRevision,
      )
      : argumentMode >= 6
      ? enumTwoArgumentPairedUpdateSource(
        enumTwoArgumentCallSources[variant], argumentMode - 5,
        argumentRevision,
      )
      : argumentMode >= 4
      ? enumTwoArgumentPairedRenameSource(
        enumTwoArgumentCallSources[variant], argumentMode - 3,
        argumentRevision,
      )
      : argumentMode === 3
      ? enumTwoArgumentPairedMacroSource(
        enumTwoArgumentCallSources[variant], argumentRevision,
      )
      : argumentMode > 0
      ? enumTwoArgumentMacroSource(
        enumTwoArgumentCallSources[variant], argumentMode,
        argumentRevision,
      )
      : enumTwoArgumentCallSources[variant];
    const text = source.source;
    const activeMacroNames = [
      null,
      argumentMode === 1
        ? enumTwoArgumentMacroNames[argumentRevision][1]
        : argumentMode === 3 || argumentMode === 5 ||
            argumentMode === 6 || argumentMode === 7 || argumentMode === 9
          ? enumTwoArgumentMacroNames[0][1]
          : argumentMode === 4 || argumentMode === 8 || argumentMode === 10
            ? enumTwoArgumentMacroNames[2][1] : null,
      argumentMode === 2
        ? enumTwoArgumentMacroNames[argumentRevision][2]
        : argumentMode === 3 || argumentMode === 4 ||
            argumentMode === 6 || argumentMode === 7 || argumentMode === 8
          ? enumTwoArgumentMacroNames[0][2]
          : argumentMode === 5 || argumentMode === 9 || argumentMode === 10
            ? enumTwoArgumentMacroNames[2][2] : null,
    ];
    const missingArgumentMode = argumentMode === 3
      ? argumentRevision
      : argumentMode === 4
        ? argumentRevision ? 2 : 0
      : argumentMode === 5
        ? argumentRevision ? 1 : 0
      : argumentMode === 6
        ? argumentRevision ? 2 : 0
      : argumentMode === 7
        ? argumentRevision ? 1 : 0
      : argumentMode === 8
        ? argumentRevision ? 2 : 0
      : argumentMode === 9
        ? argumentRevision ? 1 : 0
      : argumentMode === 10
        ? argumentRevision
      : argumentRevision === 3 ? argumentMode : 0;
    const argumentMissing = [
      false,
      (missingArgumentMode & 1) !== 0,
      (missingArgumentMode & 2) !== 0,
    ];
    const firstMissingArgumentIndex = argumentMissing[1]
      ? 1 : argumentMissing[2] ? 2 : 0;
    const argumentMetadataRevisions = [0, 0, 0];
    if (argumentMode === 1 || argumentMode === 2) {
      argumentMetadataRevisions[argumentMode] = argumentRevision;
    } else if (argumentMode === 4) {
      argumentMetadataRevisions[1] = 2;
    } else if (argumentMode === 5) {
      argumentMetadataRevisions[2] = 2;
    } else if (argumentMode === 6) {
      argumentMetadataRevisions[1] = 1;
    } else if (argumentMode === 7) {
      argumentMetadataRevisions[2] = 1;
    } else if (argumentMode === 8) {
      argumentMetadataRevisions[1] = 2;
      argumentMetadataRevisions[2] = 1;
    } else if (argumentMode === 9) {
      argumentMetadataRevisions[1] = 1;
      argumentMetadataRevisions[2] = 2;
    } else if (argumentMode === 10) {
      argumentMetadataRevisions[1] = 2;
      argumentMetadataRevisions[2] = 2;
    }
    const argumentNames = [
      null,
      activeMacroNames[1] ?? enumTwoArgumentNames[1],
      activeMacroNames[2] ?? enumTwoArgumentNames[2],
    ];
    const calleeIndex = text.lastIndexOf(enumTwoArgumentNames[0]);
    const callOpenIndex = text.indexOf(
      "(", calleeIndex + enumTwoArgumentNames[0].length,
    );
    const firstIndex = text.indexOf(
      argumentNames[1], callOpenIndex + 1,
    );
    const commaIndex = text.indexOf(
      ",", firstIndex + argumentNames[1].length,
    );
    const secondIndex = text.indexOf(
      argumentNames[2], commaIndex + 1,
    );
    const callCloseIndex = text.indexOf(
      ")", secondIndex + argumentNames[2].length,
    );
    assert.ok(calleeIndex >= 0 && callOpenIndex >= 0 && firstIndex >= 0 &&
      commaIndex >= 0 && secondIndex >= 0 && callCloseIndex >= 0,
    "enum two argument call anchors");
    const calleeStart = byteOffsetForIndex(text, calleeIndex);
    const callOpen = byteOffsetForIndex(text, callOpenIndex);
    const firstStart = byteOffsetForIndex(text, firstIndex);
    const firstEnd = firstStart + Buffer.byteLength(argumentNames[1]);
    const comma = byteOffsetForIndex(text, commaIndex);
    const secondStart = byteOffsetForIndex(text, secondIndex);
    const secondEnd = secondStart +
      Buffer.byteLength(argumentNames[2]);
    const callEnd = byteOffsetForIndex(text, callCloseIndex + 1);
    const calleeMiddle = calleeStart +
      Math.floor(Buffer.byteLength(enumTwoArgumentNames[0]) / 2);
    const cursorSteps = [
      [calleeMiddle, 0, "callee-middle"],
      [calleeStart + Buffer.byteLength(enumTwoArgumentNames[0]),
        0, "callee-end"],
      [callOpen + 1, -1, "call-open"],
      [firstStart, 1, "first-start"],
      [firstStart + Math.floor(Buffer.byteLength(argumentNames[1]) / 2),
        1, "first-middle"],
      [firstEnd, 1, "first-end"],
      [firstEnd + 1, -1, "first-after"],
      [comma, -1, "comma"],
      [comma + 1, -1, "comma-after"],
      [secondStart - 1, -1, "second-before"],
      [secondStart, 2, "second-start"],
      [secondStart +
        Math.floor(Buffer.byteLength(argumentNames[2]) / 2),
      2, "second-middle"],
      [secondEnd, 2, "second-end"],
      [secondEnd + 1, -1, "second-after"],
      [callEnd, -1, "call-end"],
      [calleeMiddle, 0, "callee-middle"],
    ];
    if (variant === 1) {
      const beforeComment = text.indexOf("/* before comma */", firstIndex);
      const afterComment = text.indexOf("/* after comma */", commaIndex);
      assert.ok(beforeComment >= 0 && afterComment >= 0,
        "enum two argument comment anchors");
      cursorSteps.push(
        [byteOffsetForIndex(text, beforeComment + 4), -1, "before-comment"],
        [byteOffsetForIndex(text, afterComment + 4), -1, "after-comment"],
      );
    } else if (variant === 2 || variant === 3) {
      const firstSplice = text.indexOf("\\", firstIndex);
      const secondSplice = text.indexOf("\\", commaIndex + 1);
      assert.ok(firstSplice >= 0 && firstSplice < commaIndex &&
        secondSplice >= 0 && secondSplice < secondIndex,
      "enum two argument splice anchors");
      cursorSteps.push(
        [byteOffsetForIndex(text, firstSplice), -1, "first-splice"],
        [byteOffsetForIndex(text, firstSplice + 1), -1,
          "first-splice-after"],
        [byteOffsetForIndex(text, secondSplice), -1, "second-splice"],
        [byteOffsetForIndex(text, secondSplice + 1), -1,
          "second-splice-after"],
      );
    }
    const enumOnly = state === 1;
    for (const [byteOffset, hoverEnumIndex, label] of
      cursorSteps) {
      const result = enumTwoArgumentCompiler.analyzeSource(
        source,
        {
          headers: {
            "enum-two-argument-call.h": enumOnly
              ? "" : enumTwoArgumentCallHeader,
          },
          cursor: {
            sourceName: source.name,
            byteOffset,
          },
        },
      );
      assert.equal(result.partial, true);
      assert.deepStrictEqual(
        result.dependencies, ["enum-two-argument-call.h"],
      );
      const enumBlockIndex = text.indexOf(
        "enum EnumTwoArgumentValues {\n",
      );
      const enumCandidates = enumTwoArgumentNames.map((name, index) => {
        const candidate = symbol(result, name, "enumConstant");
        const declarationIndex = text.indexOf(name, enumBlockIndex);
        const commentIndex = text.indexOf(
          enumTwoArgumentComments[index], enumBlockIndex,
        );
        assert.equal(candidate?.initializer.constantValue,
          enumTwoArgumentValues[index]);
        assert.equal(candidate?.declaration.sourceName,
          source.name);
        assert.equal(candidate?.declaration.start.offset, declarationIndex);
        assert.equal(candidate?.declaration.end.offset,
          declarationIndex + Buffer.byteLength(name));
        assert.equal(candidate?.documentation,
          enumTwoArgumentDocumentation[index]);
        assert.equal(candidate?.documentationRange?.sourceName,
          source.name);
        assert.equal(candidate?.documentationRange?.start.offset,
          commentIndex);
        assert.equal(candidate?.documentationRange?.end.offset,
          commentIndex + Buffer.byteLength(enumTwoArgumentComments[index]));
        return candidate;
      });
      const macroCandidate = symbol(
        result, enumTwoArgumentNames[0], "macro",
      );
      if (enumOnly) {
        assert.equal(macroCandidate, undefined);
      } else {
        const declarationIndex = enumTwoArgumentCallHeader.indexOf(
          enumTwoArgumentNames[0],
        );
        const comment = "/// enum two argument function-like macro";
        const commentIndex = enumTwoArgumentCallHeader.indexOf(comment);
        assert.equal(macroCandidate?.macro?.functionLike, true);
        assert.equal(macroCandidate?.macro?.variadic, false);
        assert.deepStrictEqual(macroCandidate?.macro?.parameters,
          ["left", "right"]);
        assert.equal(macroCandidate?.macro?.replacement,
          "( ( left ) + ( right ) + 100 )");
        assert.equal(macroCandidate?.declaration.sourceName,
          "enum-two-argument-call.h");
        assert.equal(macroCandidate?.declaration.start.offset,
          declarationIndex);
        assert.equal(macroCandidate?.declaration.end.offset,
          declarationIndex + Buffer.byteLength(enumTwoArgumentNames[0]));
        assert.equal(macroCandidate?.documentation,
          "enum two argument function-like macro");
        assert.equal(macroCandidate?.documentationRange?.sourceName,
          "enum-two-argument-call.h");
        assert.equal(macroCandidate?.documentationRange?.start.offset,
          commentIndex);
        assert.equal(macroCandidate?.documentationRange?.end.offset,
          commentIndex + Buffer.byteLength(comment));
      }
      const argumentMacroCandidates = [undefined, undefined, undefined];
      for (let macroIndex = 1; macroIndex < 3; macroIndex++) {
        const argumentMacroName = activeMacroNames[macroIndex];
        if (!argumentMacroName) continue;
        argumentMacroCandidates[macroIndex] = symbol(
          result, argumentMacroName, "macro",
        );
        if (argumentMissing[macroIndex]) {
          assert.equal(argumentMacroCandidates[macroIndex], undefined);
          continue;
        }
        const declarationIndex = text.indexOf(argumentMacroName);
        const comment = enumTwoArgumentMacroComments
          [argumentMetadataRevisions[macroIndex]][macroIndex];
        const commentIndex = text.indexOf(comment);
        const argumentMacroCandidate = argumentMacroCandidates[macroIndex];
        assert.equal(argumentMacroCandidate?.macro?.functionLike, false);
        assert.equal(argumentMacroCandidate?.macro?.variadic, false);
        assert.deepStrictEqual(argumentMacroCandidate?.macro?.parameters, []);
        assert.equal(argumentMacroCandidate?.macro?.replacement,
          enumTwoArgumentMacroValues
            [argumentMetadataRevisions[macroIndex]][macroIndex]);
        assert.equal(argumentMacroCandidate?.declaration.sourceName,
          source.name);
        assert.equal(argumentMacroCandidate?.declaration.start.offset,
          declarationIndex);
        assert.equal(argumentMacroCandidate?.declaration.end.offset,
          declarationIndex + Buffer.byteLength(argumentMacroName));
        assert.equal(argumentMacroCandidate?.documentation,
          enumTwoArgumentMacroDocumentation
            [argumentMetadataRevisions[macroIndex]][macroIndex]);
        assert.equal(argumentMacroCandidate?.documentationRange?.sourceName,
          source.name);
        assert.equal(argumentMacroCandidate?.documentationRange?.start.offset,
          commentIndex);
        assert.equal(argumentMacroCandidate?.documentationRange?.end.offset,
          commentIndex + Buffer.byteLength(comment));
      }
      if (argumentMode > 0) {
        for (const revisionNames of enumTwoArgumentMacroNames) {
          for (const inactiveName of revisionNames.slice(1)) {
            const active = activeMacroNames.some((name, macroIndex) =>
              !argumentMissing[macroIndex] && name === inactiveName,
            );
            if (!active) {
              assert.equal(symbol(result, inactiveName, "macro"), undefined);
            }
          }
        }
      }
      const derived = symbol(
        result, "ENUM_TWO_ARGUMENT_DERIVED", "enumConstant",
      );
      const argumentUpdated = argumentMetadataRevisions[1] === 1 ||
        argumentMetadataRevisions[2] === 1;
      if (enumOnly || firstMissingArgumentIndex) {
        assert.equal(derived, undefined);
      } else {
        assert.equal(derived?.initializer.constantValue,
          argumentUpdated ? "113" : "103");
      }
      const expectedDiagnosticCount = !enumOnly && firstMissingArgumentIndex
        ? 1
        : enumOnly && byteOffset >= callOpen ? 1 : 0;
      assert.equal(result.diagnostics.length, expectedDiagnosticCount);
      if (!enumOnly && firstMissingArgumentIndex) {
        assert.equal(result.diagnostics[0].code, "E3066");
        assert.match(result.diagnostics[0].message,
          new RegExp(activeMacroNames[firstMissingArgumentIndex]));
        if (argumentMissing[1] && argumentMissing[2]) {
          assert.doesNotMatch(result.diagnostics[0].message,
            new RegExp(activeMacroNames[2]));
        }
        assert.equal(result.diagnostics[0].start.offset, calleeStart);
        assert.equal(result.diagnostics[0].end.offset, callOpen);
      } else if (expectedDiagnosticCount) {
        assert.equal(result.diagnostics[0].code, "E3102");
        assert.equal(result.diagnostics[0].start.offset,
          callOpen);
        assert.equal(result.diagnostics[0].end.offset,
          callOpen + 1);
      }
      const expectedHover = hoverEnumIndex < 0
        ? null
        : hoverEnumIndex === 0 && !enumOnly
          ? macroCandidate
          : hoverEnumIndex > 0 && activeMacroNames[hoverEnumIndex]
            ? argumentMacroCandidates[hoverEnumIndex]
            : enumCandidates[hoverEnumIndex];
      if (expectedHover) {
        assert.equal(result.hover?.kind, expectedHover.kind);
        assert.deepStrictEqual(result.hover?.declaration,
          expectedHover.declaration);
      } else {
        assert.equal(result.hover, null);
      }
      const key =
        `${variant}:${state}:${argumentMode}:${argumentRevision}:${byteOffset}`;
      const firstResult = enumTwoArgumentFirstResults.get(key);
      if (firstResult) {
        assert.deepStrictEqual(result, firstResult,
          `Wasm enum two argument call retained stale state for ${variant}, ${state}, ${argumentMode}, ${argumentRevision}, ${label}`);
      } else {
        enumTwoArgumentFirstResults.set(key, result);
        assert.deepStrictEqual(
          result,
          JSON.parse(execFileSync(
            nativeAnalysisPath,
            ["--enum-two-argument-call-parity-json", String(variant),
              String(state), String(argumentMode), String(argumentRevision),
              String(byteOffset)],
            { encoding: "utf8" },
          )),
          `native and Wasm enum two argument call differ for ${variant}, ${state}, ${argumentMode}, ${argumentRevision}, ${label}`,
        );
      }
    }
  }
} finally {
  enumTwoArgumentCompiler.dispose();
}

const enumThreeArgumentCompiler = await createCompiler(wasmModule);
const enumThreeArgumentFirstResults = new Map();
try {
  for (const [variant, missingArgumentMode] of [
    [0, 0], [0, 2], [0, 0], [0, 4], [0, 0],
    [1, 0], [1, 4], [1, 6], [1, 2], [1, 0], [0, 0],
    [2, 0], [2, 2], [2, 0],
    [3, 0], [3, 2], [3, 0], [2, 0],
    [4, 0], [4, 1], [4, 5], [4, 4], [4, 0],
    [5, 0], [5, 4], [5, 5], [5, 1], [5, 0], [4, 0],
    [6, 0], [6, 1], [6, 5], [6, 4], [6, 0], [4, 0],
    [5, 0], [7, 0], [7, 4], [7, 5], [7, 1], [7, 0], [5, 0], [4, 0],
    [6, 0], [6, 1], [8, 1], [8, 5], [8, 4], [8, 1], [8, 0], [6, 0],
    [7, 0], [7, 4], [9, 4], [9, 5], [9, 1], [9, 4], [9, 0], [7, 0],
    [6, 0],
    [8, 0], [8, 1], [10, 1], [10, 5], [10, 4], [10, 1], [10, 0],
    [8, 0],
    [9, 0], [9, 4], [11, 4], [11, 5], [11, 1], [11, 4], [11, 0],
    [9, 0], [8, 0],
  ]) {
    const source = enumThreeArgumentMacroSource(
      enumThreeArgumentCallSources[variant], missingArgumentMode,
    );
    const outerEnumArguments = variant >= 2 && variant < 4;
    const middleEnumArgument = variant >= 4;
    const renamedMiddleEnumArgument = variant >= 6;
    const updatedMiddleEnumArgument = variant >= 8;
    const updatedMacroArguments = variant >= 10;
    const argumentNames = renamedMiddleEnumArgument
      ? enumThreeArgumentRenamedMiddleEnumNames
      : middleEnumArgument
        ? enumThreeArgumentMiddleEnumNames
        : outerEnumArguments
          ? enumThreeArgumentMixedNames : enumThreeArgumentMacroNames;
    const argumentValues = updatedMacroArguments
      ? enumThreeArgumentUpdatedMacroValues
      : updatedMiddleEnumArgument
        ? enumThreeArgumentUpdatedMiddleEnumValues
        : middleEnumArgument
          ? enumThreeArgumentMiddleEnumValues
          : outerEnumArguments
            ? enumThreeArgumentMixedValues : enumThreeArgumentMacroValues;
    const argumentDocumentation = updatedMiddleEnumArgument
      ? enumThreeArgumentUpdatedMiddleEnumDocumentation
      : middleEnumArgument
        ? enumThreeArgumentMiddleEnumDocumentation
        : outerEnumArguments
          ? enumThreeArgumentMixedDocumentation
          : enumThreeArgumentMacroDocumentation;
    const argumentComments = updatedMiddleEnumArgument
      ? enumThreeArgumentUpdatedMiddleEnumComments
      : middleEnumArgument
        ? enumThreeArgumentMiddleEnumComments
        : outerEnumArguments
          ? enumThreeArgumentMixedComments : enumThreeArgumentMacroComments;
    const text = source.source;
    const calleeName = "ENUM_THREE_ARGUMENT_CALL";
    const calleeIndex = text.lastIndexOf(calleeName);
    const callOpenIndex = text.indexOf("(", calleeIndex + calleeName.length);
    const argumentIndexes = [0, 0, 0];
    const commaIndexes = [0, 0];
    argumentIndexes[0] = text.indexOf(
      argumentNames[0], callOpenIndex + 1,
    );
    commaIndexes[0] = text.indexOf(
      ",", argumentIndexes[0] + argumentNames[0].length,
    );
    argumentIndexes[1] = text.indexOf(
      argumentNames[1], commaIndexes[0] + 1,
    );
    commaIndexes[1] = text.indexOf(
      ",", argumentIndexes[1] + argumentNames[1].length,
    );
    argumentIndexes[2] = text.indexOf(
      argumentNames[2], commaIndexes[1] + 1,
    );
    const callCloseIndex = text.indexOf(
      ")", argumentIndexes[2] + argumentNames[2].length,
    );
    assert.ok(calleeIndex >= 0 && callOpenIndex >= 0 &&
      argumentIndexes.every((index) => index >= 0) &&
      commaIndexes.every((index) => index >= 0) && callCloseIndex >= 0,
    "enum three argument call anchors");
    const calleeStart = byteOffsetForIndex(text, calleeIndex);
    const callOpen = byteOffsetForIndex(text, callOpenIndex);
    const argumentStarts = argumentIndexes.map(
      (index) => byteOffsetForIndex(text, index),
    );
    const argumentEnds = argumentStarts.map(
      (start, index) => start + Buffer.byteLength(argumentNames[index]),
    );
    const commas = commaIndexes.map(
      (index) => byteOffsetForIndex(text, index),
    );
    const cursorSteps = [
      [calleeStart + Math.floor(Buffer.byteLength(calleeName) / 2),
        -2, "callee-middle"],
      [callOpen + 1, 0, "call-open"],
      [argumentStarts[0], 0, "first-start"],
      [argumentStarts[0] +
        Math.floor(Buffer.byteLength(argumentNames[0]) / 2),
      0, "first-middle"],
      [argumentEnds[0], 0, "first-end"],
      [commas[0], -1, "first-comma"],
      [argumentStarts[1], 1, "middle-start"],
      [argumentStarts[1] +
        Math.floor(Buffer.byteLength(argumentNames[1]) / 2),
      1, "middle-middle"],
      [argumentEnds[1], 1, "middle-end"],
      [commas[1], -1, "second-comma"],
      [argumentStarts[2], 2, "last-start"],
      [argumentStarts[2] +
        Math.floor(Buffer.byteLength(argumentNames[2]) / 2),
      2, "last-middle"],
      [argumentEnds[2], 2, "last-end"],
      [byteOffsetForIndex(text, callCloseIndex + 1), -1, "call-end"],
    ];
    if ((variant & 1) === 0) {
      for (const [comment, label] of [
        ["/* first comma */", "first-comment"],
        ["/* second comma */", "second-comment"],
      ]) {
        const commentIndex = text.indexOf(comment, callOpenIndex);
        assert.ok(commentIndex >= 0, "enum three argument comment anchor");
        cursorSteps.push([
          byteOffsetForIndex(text, commentIndex + 4), -1, label,
        ]);
      }
    } else {
      const spliceIndexes = [
        text.indexOf("\\", argumentIndexes[0]),
        text.indexOf("\\", argumentIndexes[1]),
      ];
      assert.ok(spliceIndexes[0] >= 0 &&
        spliceIndexes[0] < commaIndexes[0] && spliceIndexes[1] >= 0 &&
        spliceIndexes[1] < commaIndexes[1],
      "enum three argument splice anchors");
      for (let spliceIndex = 0; spliceIndex < 2; spliceIndex++) {
        cursorSteps.push(
          [byteOffsetForIndex(text, spliceIndexes[spliceIndex]), -1,
            `${spliceIndex}-splice`],
          [byteOffsetForIndex(text, spliceIndexes[spliceIndex] + 1), -1,
            `${spliceIndex}-splice-after`],
        );
      }
    }
    for (const [byteOffset, hoverIndex, label] of cursorSteps) {
      const result = enumThreeArgumentCompiler.analyzeSource(source, {
        headers: {
          "enum-three-argument-call.h": enumThreeArgumentCallHeader,
        },
        cursor: {sourceName: source.name, byteOffset},
      });
      assert.equal(result.partial, true);
      assert.deepStrictEqual(
        result.dependencies, ["enum-three-argument-call.h"],
      );
      const calleeCandidate = symbol(result, calleeName, "macro");
      assert.equal(calleeCandidate?.macro?.functionLike, true);
      assert.equal(calleeCandidate?.macro?.variadic, false);
      assert.deepStrictEqual(
        calleeCandidate?.macro?.parameters, ["first", "middle", "last"],
      );
      assert.equal(calleeCandidate?.macro?.replacement,
        "( ( first ) + ( middle ) + ( last ) + 100 )");
      const argumentCandidates = argumentNames.map((name, index) => {
        const enumArgument = (outerEnumArguments && index !== 1) ||
          (middleEnumArgument && index === 1);
        const candidate = symbol(
          result, name, enumArgument ? "enumConstant" : "macro",
        );
        if ((missingArgumentMode & (1 << index)) !== 0) {
          assert.equal(candidate, undefined);
          return undefined;
        }
        const declarationIndex = text.indexOf(name);
        const commentIndex = text.indexOf(argumentComments[index]);
        if (enumArgument) {
          assert.equal(candidate?.initializer.constantValue,
            argumentValues[index]);
        } else {
          assert.equal(candidate?.macro?.functionLike, false);
          assert.equal(candidate?.macro?.variadic, false);
          assert.deepStrictEqual(candidate?.macro?.parameters, []);
          assert.equal(candidate?.macro?.replacement, argumentValues[index]);
        }
        assert.equal(candidate?.documentation,
          argumentDocumentation[index]);
        assert.equal(candidate?.declaration.sourceName, source.name);
        assert.equal(candidate?.declaration.start.offset, declarationIndex);
        assert.equal(candidate?.declaration.end.offset,
          declarationIndex + Buffer.byteLength(name));
        assert.equal(candidate?.documentationRange?.start.offset,
          commentIndex);
        assert.equal(candidate?.documentationRange?.end.offset,
          commentIndex + Buffer.byteLength(argumentComments[index]));
        return candidate;
      });
      if (middleEnumArgument) {
        const inactiveMiddleEnumName = renamedMiddleEnumArgument
          ? enumThreeArgumentMiddleEnumNames[1]
          : enumThreeArgumentRenamedMiddleEnumNames[1];
        assert.equal(symbol(
          result, inactiveMiddleEnumName, "enumConstant",
        ), undefined);
      }
      const derived = symbol(
        result, "ENUM_THREE_ARGUMENT_DERIVED", "enumConstant",
      );
      if (missingArgumentMode) {
        assert.equal(derived, undefined);
      } else {
        assert.equal(derived?.initializer.constantValue,
          updatedMacroArguments
            ? "127" : updatedMiddleEnumArgument
              ? "113" : middleEnumArgument
              ? "110" : outerEnumArguments ? "111" : "106");
      }
      const firstMissingArgumentIndex = [0, 1, 2].find(
        (index) => (missingArgumentMode & (1 << index)) !== 0,
      );
      assert.equal(result.diagnostics.length,
        missingArgumentMode ? 1 : 0);
      if (missingArgumentMode) {
        assert.equal(result.diagnostics[0].code, "E3066");
        assert.match(result.diagnostics[0].message,
          new RegExp(argumentNames[firstMissingArgumentIndex]));
        for (let argumentIndex = firstMissingArgumentIndex + 1;
          argumentIndex < 3; argumentIndex++) {
          if ((missingArgumentMode & (1 << argumentIndex)) !== 0) {
            assert.doesNotMatch(result.diagnostics[0].message,
              new RegExp(argumentNames[argumentIndex]));
          }
        }
        assert.equal(result.diagnostics[0].start.offset, calleeStart);
        assert.equal(result.diagnostics[0].end.offset, callOpen);
      }
      const expectedHover = hoverIndex === -2
        ? calleeCandidate
        : hoverIndex >= 0 ? argumentCandidates[hoverIndex] : null;
      if (expectedHover) {
        assert.equal(result.hover?.kind, expectedHover.kind);
        assert.deepStrictEqual(result.hover?.declaration,
          expectedHover.declaration);
      } else {
        assert.equal(result.hover, null);
      }
      const key = `${variant}:${missingArgumentMode}:${byteOffset}`;
      const firstResult = enumThreeArgumentFirstResults.get(key);
      if (firstResult) {
        assert.deepStrictEqual(result, firstResult,
          `Wasm enum three argument call retained stale state for ${variant}, ${missingArgumentMode}, ${label}`);
      } else {
        enumThreeArgumentFirstResults.set(key, result);
        assert.deepStrictEqual(result, JSON.parse(execFileSync(
          nativeAnalysisPath,
          ["--enum-three-argument-call-parity-json", String(variant),
            String(missingArgumentMode), String(byteOffset)],
          {encoding: "utf8"},
        )),
        `native and Wasm enum three argument call differ for ${variant}, ${missingArgumentMode}, ${label}`);
      }
    }
  }
} finally {
  enumThreeArgumentCompiler.dispose();
}

const initializerDesignatorOperandHoverSource = {
  name: "initializer-designator-operand.c",
  source: "/// initializer designator macro documentation\n" +
    "#define INITIALIZER_DESIGNATOR_MACRO 5\n" +
    "enum InitializerDesignatorValue {\n" +
    "  INITIALIZER_DESIGNATOR_A = 2,\n" +
    "  INITIALIZER_DESIGNATOR_B = 3,\n" +
    "  INITIALIZER_DESIGNATOR_C = 4,\n" +
    "  INITIALIZER_DESIGNATOR_CONDITION = 1\n" +
    "};\n" +
    "int initializer_designator_direct[16] = { [INITIALIZER_DESIGNATOR_A] = 1 };\n" +
    "int initializer_designator_unary[16] = { [+INITIALIZER_DESIGNATOR_A] = 1 };\n" +
    "int initializer_designator_binary[16] = { [INITIALIZER_DESIGNATOR_A + INITIALIZER_DESIGNATOR_B] = 1 };\n" +
    "int initializer_designator_grouped[16] = { [(INITIALIZER_DESIGNATOR_C)] = 1 };\n" +
    "int initializer_designator_conditional[16] = { [INITIALIZER_DESIGNATOR_CONDITION ? INITIALIZER_DESIGNATOR_A : INITIALIZER_DESIGNATOR_B] = 1 };\n" +
    "int initializer_designator_macro[16] = { [INITIALIZER_DESIGNATOR_MACRO] = 1 };\n" +
    "int initializer_designator_comment[16] = { [/* expression gap */ INITIALIZER_DESIGNATOR_A] = 1 };\n" +
    "int initializer_designator_splice_lf[16] = { [\\\n" +
    "INITIALIZER_DESIGNATOR_B] = 1 };\n" +
    "int initializer_designator_splice_crlf[16] = { [\\\r\n" +
    "INITIALIZER_DESIGNATOR_C] = 1 };\r\n" +
    "int initializer_designator_nested[2][16] = { [1] = { [INITIALIZER_DESIGNATOR_A] = 1 } };\n" +
    "struct InitializerDesignatorRecord { int value; };\n" +
    "struct InitializerDesignatorRecord initializer_designator_member_chain[8] = { [INITIALIZER_DESIGNATOR_A].value = 1 };\n" +
    "int initializer_designator_array_chain[8][8] = { [INITIALIZER_DESIGNATOR_A][INITIALIZER_DESIGNATOR_B] = 1 };\n" +
    "int initializer_designator_multi[16] = { [INITIALIZER_DESIGNATOR_B] = 1 }, initializer_designator_later[16];\n" +
    "static int initializer_designator_block(int designator_parameter) {\n" +
    "  enum { INITIALIZER_DESIGNATOR_LOCAL = 6 };\n" +
    "  int designator_before = designator_parameter;\n" +
    "  int designator_local[16] = { [INITIALIZER_DESIGNATOR_LOCAL] = 1 };\n" +
    "  int designator_after = designator_before;\n" +
    "  return designator_local[INITIALIZER_DESIGNATOR_LOCAL] + designator_after;\n" +
    "}\n",
};
const initializerDesignatorOperandCases = [
  ["[INITIALIZER_DESIGNATOR_A] = 1", "INITIALIZER_DESIGNATOR_A", "enumConstant", 0],
  ["[+INITIALIZER_DESIGNATOR_A] = 1", "INITIALIZER_DESIGNATOR_A", "enumConstant", 0],
  ["[INITIALIZER_DESIGNATOR_A + INITIALIZER_DESIGNATOR_B] = 1",
    "INITIALIZER_DESIGNATOR_A", "enumConstant", 0],
  ["+ INITIALIZER_DESIGNATOR_B] = 1", "INITIALIZER_DESIGNATOR_B", "enumConstant", 0],
  ["[(INITIALIZER_DESIGNATOR_C)] = 1", "INITIALIZER_DESIGNATOR_C", "enumConstant", 0],
  ["? INITIALIZER_DESIGNATOR_A : INITIALIZER_DESIGNATOR_B] = 1",
    "INITIALIZER_DESIGNATOR_B", "enumConstant", 0],
  ["[INITIALIZER_DESIGNATOR_MACRO] = 1", "INITIALIZER_DESIGNATOR_MACRO", "macro", 0],
  ["[/* expression gap */ INITIALIZER_DESIGNATOR_A] = 1",
    "INITIALIZER_DESIGNATOR_A", "enumConstant", 0],
  ["[\\\nINITIALIZER_DESIGNATOR_B] = 1", "INITIALIZER_DESIGNATOR_B", "enumConstant", 0],
  ["[\\\r\nINITIALIZER_DESIGNATOR_C] = 1", "INITIALIZER_DESIGNATOR_C", "enumConstant", 0],
  ["[1] = { [INITIALIZER_DESIGNATOR_A] = 1 }",
    "INITIALIZER_DESIGNATOR_A", "enumConstant", 0],
  ["[INITIALIZER_DESIGNATOR_A].value = 1",
    "INITIALIZER_DESIGNATOR_A", "enumConstant", 0],
  ["[INITIALIZER_DESIGNATOR_A][INITIALIZER_DESIGNATOR_B] = 1",
    "INITIALIZER_DESIGNATOR_A", "enumConstant", 0],
  ["][INITIALIZER_DESIGNATOR_B] = 1",
    "INITIALIZER_DESIGNATOR_B", "enumConstant", 0],
  ["initializer_designator_multi[16] = { [INITIALIZER_DESIGNATOR_B] = 1 }",
    "INITIALIZER_DESIGNATOR_B", "enumConstant", 1],
  ["designator_local[16] = { [INITIALIZER_DESIGNATOR_LOCAL] = 1 }",
    "INITIALIZER_DESIGNATOR_LOCAL", "enumConstant", 2],
];
for (const [fragmentText, name, kind, boundaryCase] of
  initializerDesignatorOperandCases) {
  const fragmentIndex = initializerDesignatorOperandHoverSource.source.indexOf(
    fragmentText,
  );
  const useIndex = initializerDesignatorOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `initializer designator operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    initializerDesignatorOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(
      initializerDesignatorOperandHoverSource,
      {
        cursor: {
          sourceName: initializerDesignatorOperandHoverSource.name,
          byteOffset,
        },
      },
    );
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} initializer designator operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} initializer designator operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} initializer designator operand hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} initializer designator operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} initializer designator operand declaration`);
    if (kind === "enumConstant")
      assert.ok(["2", "3", "4", "6"].includes(
        result.hover?.initializer.constantValue,
      ));
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "5");
      assert.equal(result.hover?.documentation,
        "initializer designator macro documentation");
    }
    if (boundaryCase === 1)
      assert.equal(symbol(result, "initializer_designator_later", "object"),
        undefined, "later comma declarator remains invisible");
    if (boundaryCase === 2) {
      assert.ok(symbol(result, "designator_parameter", "parameter"));
      assert.ok(symbol(result, "designator_before", "object"));
      assert.equal(symbol(result, "designator_after", "object"), undefined,
        "block designator preserves cursor lookup point");
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--initializer-designator-operand-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm initializer designator operand differ for ${name} at ${delta}`,
    );
  }
}

const freshInitializerDesignatorCompiler = await createCompiler(wasmModule);
try {
  for (const [fragmentText, name, kind] of [
    ["[1] = { [INITIALIZER_DESIGNATOR_A] = 1 }",
      "INITIALIZER_DESIGNATOR_A", "enumConstant"],
    ["designator_local[16] = { [INITIALIZER_DESIGNATOR_LOCAL] = 1 }",
      "INITIALIZER_DESIGNATOR_LOCAL", "enumConstant"],
  ]) {
    const fragmentIndex = initializerDesignatorOperandHoverSource.source.indexOf(
      fragmentText,
    );
    const useIndex = initializerDesignatorOperandHoverSource.source.indexOf(
      name, fragmentIndex,
    );
    const result = freshInitializerDesignatorCompiler.analyzeSource(
      initializerDesignatorOperandHoverSource,
      {
        cursor: {
          sourceName: initializerDesignatorOperandHoverSource.name,
          byteOffset: byteOffsetForIndex(
            initializerDesignatorOperandHoverSource.source, useIndex,
          ) + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, false,
      `fresh ${name} initializer designator operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `fresh ${name} initializer designator operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `fresh ${name} initializer designator operand hover`);
    assert.equal(result.hover?.kind, kind,
      `fresh ${name} initializer designator operand kind`);
  }
} finally {
  freshInitializerDesignatorCompiler.dispose();
}

const compoundLiteralDesignatorOperandHoverSource = {
  name: "compound-literal-designator-operand.c",
  source: "/// compound literal designator macro documentation\n" +
    "#define COMPOUND_LITERAL_DESIGNATOR_MACRO 5\n" +
    "typedef int CompoundLiteralDesignatorArray[8];\n" +
    "enum CompoundLiteralDesignatorValue {\n" +
    "  COMPOUND_LITERAL_DESIGNATOR_A = 2,\n" +
    "  COMPOUND_LITERAL_DESIGNATOR_B = 3,\n" +
    "  COMPOUND_LITERAL_DESIGNATOR_C = 4,\n" +
    "  COMPOUND_LITERAL_DESIGNATOR_CONDITION = 1\n" +
    "};\n" +
    "struct CompoundLiteralDesignatorRecord { int values[8]; };\n" +
    "int *compound_literal_designator_file_direct = (int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n" +
    "int *compound_literal_designator_file_unary = (int[8]){ " +
    "[+COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n" +
    "int *compound_literal_designator_file_binary = (int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_A + COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n" +
    "int *compound_literal_designator_file_grouped = (int[8]){ " +
    "[(COMPOUND_LITERAL_DESIGNATOR_C)] = 1 };\n" +
    "int *compound_literal_designator_file_conditional = (int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_CONDITION ? " +
    "COMPOUND_LITERAL_DESIGNATOR_A : COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n" +
    "int *compound_literal_designator_file_macro = (int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_MACRO] = 1 };\n" +
    "int *compound_literal_designator_file_comment = (int[8]){ " +
    "[/* expression gap */ COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n" +
    "int *compound_literal_designator_file_splice_lf = (int[8]){ [\\\n" +
    "COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n" +
    "int *compound_literal_designator_file_splice_crlf = (int[8]){ [\\\r\n" +
    "COMPOUND_LITERAL_DESIGNATOR_C] = 1 };\r\n" +
    "int *compound_literal_designator_file_typedef = " +
    "(CompoundLiteralDesignatorArray){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n" +
    "int (*compound_literal_designator_file_nested)[8] = (int[2][8]){ " +
    "[1] = { [COMPOUND_LITERAL_DESIGNATOR_B] = 1 } };\n" +
    "struct CompoundLiteralDesignatorRecord " +
    "*compound_literal_designator_file_member = " +
    "&(struct CompoundLiteralDesignatorRecord){ " +
    ".values[COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n" +
    "int (*compound_literal_designator_file_chain)[8] = (int[8][8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_A][COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n" +
    "int *compound_literal_designator_file_multi = (int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_C] = 1 }, " +
    "*compound_literal_designator_file_later;\n" +
    "int compound_literal_designator_file_after;\n" +
    "static int compound_literal_designator_take(int *values) { " +
    "return values[0]; }\n" +
    "static int compound_literal_designator_block(int designator_parameter) {\n" +
    "  int designator_before = designator_parameter;\n" +
    "  int designator_value = ((int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_A] = 1 })" +
    "[COMPOUND_LITERAL_DESIGNATOR_A];\n" +
    "  int designator_call = compound_literal_designator_take((int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_B] = 1 });\n" +
    "  int *designator_multi = (int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_C] = 1 }, *designator_later;\n" +
    "  int designator_after = designator_before;\n" +
    "  return designator_value + designator_call + designator_after +\n" +
    "         (designator_multi != 0) + (designator_later != 0);\n" +
    "}\n",
};

const compoundLiteralDesignatorOperandCases = [
  ["file_direct = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_A] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 0],
  ["file_unary = (int[8]){ [+COMPOUND_LITERAL_DESIGNATOR_A] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 0],
  ["file_binary = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_A +",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 0],
  ["+ COMPOUND_LITERAL_DESIGNATOR_B] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_B", "enumConstant", 0],
  ["file_grouped = (int[8]){ [(COMPOUND_LITERAL_DESIGNATOR_C)] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_C", "enumConstant", 0],
  ["? COMPOUND_LITERAL_DESIGNATOR_A : COMPOUND_LITERAL_DESIGNATOR_B] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_B", "enumConstant", 0],
  ["file_macro = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_MACRO] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_MACRO", "macro", 0],
  ["[/* expression gap */ COMPOUND_LITERAL_DESIGNATOR_A] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 0],
  ["[\\\nCOMPOUND_LITERAL_DESIGNATOR_B] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_B", "enumConstant", 0],
  ["[\\\r\nCOMPOUND_LITERAL_DESIGNATOR_C] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_C", "enumConstant", 0],
  ["(CompoundLiteralDesignatorArray){ [COMPOUND_LITERAL_DESIGNATOR_A] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 0],
  ["[1] = { [COMPOUND_LITERAL_DESIGNATOR_B] = 1 }",
    "COMPOUND_LITERAL_DESIGNATOR_B", "enumConstant", 0],
  [".values[COMPOUND_LITERAL_DESIGNATOR_A] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 0],
  ["file_chain)[8] = (int[8][8]){ [COMPOUND_LITERAL_DESIGNATOR_A][",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 0],
  ["][COMPOUND_LITERAL_DESIGNATOR_B] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_B", "enumConstant", 0],
  ["file_multi = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_C] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_C", "enumConstant", 1],
  ["designator_value = ((int[8]){ [COMPOUND_LITERAL_DESIGNATOR_A] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_A", "enumConstant", 2],
  ["compound_literal_designator_take((int[8]){ " +
    "[COMPOUND_LITERAL_DESIGNATOR_B] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_B", "enumConstant", 2],
  ["designator_multi = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_C] = 1",
    "COMPOUND_LITERAL_DESIGNATOR_C", "enumConstant", 3],
];

for (const [fragmentText, name, kind, boundaryCase] of
  compoundLiteralDesignatorOperandCases) {
  const fragmentIndex =
    compoundLiteralDesignatorOperandHoverSource.source.indexOf(fragmentText);
  const useIndex =
    compoundLiteralDesignatorOperandHoverSource.source.indexOf(
      name, fragmentIndex,
    );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `compound literal designator operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    compoundLiteralDesignatorOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(
      compoundLiteralDesignatorOperandHoverSource,
      {
        cursor: {
          sourceName: compoundLiteralDesignatorOperandHoverSource.name,
          byteOffset,
        },
      },
    );
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} compound literal designator unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} compound literal designator diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} compound literal designator hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} compound literal designator kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} compound literal designator declaration`);
    if (kind === "enumConstant")
      assert.ok(["2", "3", "4"].includes(
        result.hover?.initializer.constantValue,
      ));
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "5");
      assert.equal(result.hover?.documentation,
        "compound literal designator macro documentation");
    }
    if (boundaryCase <= 1)
      assert.equal(symbol(
        result, "compound_literal_designator_file_after", "object",
      ), undefined, "later file object remains invisible");
    if (boundaryCase === 1)
      assert.equal(symbol(
        result, "compound_literal_designator_file_later", "object",
      ), undefined, "later file comma declarator remains invisible");
    if (boundaryCase >= 2) {
      assert.ok(symbol(result, "designator_parameter", "parameter"));
      assert.ok(symbol(result, "designator_before", "object"));
      assert.equal(symbol(result, "designator_after", "object"), undefined,
        "compound literal block lookup point");
    }
    if (boundaryCase === 3)
      assert.equal(symbol(result, "designator_later", "object"), undefined,
        "later block comma declarator remains invisible");
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--compound-literal-designator-operand-hover-parity-json",
          String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm compound literal designator differ for ${name} at ${delta}`,
    );
  }
}

const freshCompoundLiteralDesignatorCompiler = await createCompiler(wasmModule);
try {
  for (const caseIndex of [11, 18]) {
    const [fragmentText, name, kind] =
      compoundLiteralDesignatorOperandCases[caseIndex];
    const fragmentIndex =
      compoundLiteralDesignatorOperandHoverSource.source.indexOf(fragmentText);
    const useIndex =
      compoundLiteralDesignatorOperandHoverSource.source.indexOf(
        name, fragmentIndex,
      );
    const result = freshCompoundLiteralDesignatorCompiler.analyzeSource(
      compoundLiteralDesignatorOperandHoverSource,
      {
        cursor: {
          sourceName: compoundLiteralDesignatorOperandHoverSource.name,
          byteOffset: byteOffsetForIndex(
            compoundLiteralDesignatorOperandHoverSource.source, useIndex,
          ) + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, false,
      `fresh ${name} compound literal designator unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `fresh ${name} compound literal designator diagnostics`);
    assert.equal(result.hover?.name, name,
      `fresh ${name} compound literal designator hover`);
    assert.equal(result.hover?.kind, kind,
      `fresh ${name} compound literal designator kind`);
  }
} finally {
  freshCompoundLiteralDesignatorCompiler.dispose();
}

for (const input of [
  {
    name: "invalid-compound-literal-designator.c",
    source: "enum { INDEX = 2 }; int f(void) { " +
      "return ((int[4]){ [INDEX] }); }\n",
  },
  {
    name: "invalid-compound-literal-designator.c",
    source: "enum { INDEX = 2 }; int f(void) { " +
      "return ((int[4]){ [INDEX] = 7;\n",
  },
]) {
  let invalidResult = null;
  let invalidError = null;
  const useIndex = input.source.indexOf("[INDEX]") + 3;
  try {
    invalidResult = compiler.analyzeSource(input, {
      cursor: { sourceName: input.name, byteOffset: useIndex },
    });
  } catch (error) {
    invalidError = error;
  }
  assert.ok(
    invalidResult
      ? invalidResult.partial
      : invalidError?.name === "AgcLanguageAnalysisError",
    `invalid compound literal designator became complete: ${JSON.stringify(
      invalidResult || invalidError,
    )}`,
  );
}

const compoundLiteralReuseName = "COMPOUND_LITERAL_DESIGNATOR_A";
const compoundLiteralReuseFragment =
  compoundLiteralDesignatorOperandHoverSource.source.indexOf(
    "file_direct = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_A] = 1",
  );
const compoundLiteralReuseUse =
  compoundLiteralDesignatorOperandHoverSource.source.indexOf(
    compoundLiteralReuseName, compoundLiteralReuseFragment,
  );
const compoundLiteralReuseResult = compiler.analyzeSource(
  compoundLiteralDesignatorOperandHoverSource,
  {
    cursor: {
      sourceName: compoundLiteralDesignatorOperandHoverSource.name,
      byteOffset: byteOffsetForIndex(
        compoundLiteralDesignatorOperandHoverSource.source,
        compoundLiteralReuseUse,
      ) + 3,
    },
  },
);
assert.equal(compoundLiteralReuseResult.partial, false,
  "compound literal designator compiler did not recover after failure");
assert.deepStrictEqual(compoundLiteralReuseResult.diagnostics, [],
  "compound literal designator reuse diagnostics");

const typeNameArrayBoundOperandHoverSource = {
  name: "type-name-array-bound-operand.c",
  source: "/// type-name array bound macro documentation\n" +
    "#define TYPE_NAME_ARRAY_BOUND_MACRO 5\n" +
    "enum TypeNameArrayBoundValue {\n" +
    "  TYPE_NAME_ARRAY_BOUND_A = 2,\n" +
    "  TYPE_NAME_ARRAY_BOUND_B = 3,\n" +
    "  TYPE_NAME_ARRAY_BOUND_C = 4,\n" +
    "  TYPE_NAME_ARRAY_BOUND_CONDITION = 1\n" +
    "};\n" +
    "int type_name_array_bound_values[8];\n" +
    "int type_name_array_bound_file = sizeof(int[TYPE_NAME_ARRAY_BOUND_A]), type_name_array_bound_later;\n" +
    "static int type_name_array_bound_block(int bound_parameter) {\n" +
    "  enum { TYPE_NAME_ARRAY_BOUND_LOCAL = 6 };\n" +
    "  int bound_before = bound_parameter;\n" +
    "  int bound_sizeof_direct = sizeof(int[TYPE_NAME_ARRAY_BOUND_A]);\n" +
    "  int bound_sizeof_unary = sizeof(int[+TYPE_NAME_ARRAY_BOUND_A]);\n" +
    "  int bound_sizeof_binary = sizeof(int[TYPE_NAME_ARRAY_BOUND_A + TYPE_NAME_ARRAY_BOUND_B]);\n" +
    "  int bound_sizeof_grouped = sizeof(int[(TYPE_NAME_ARRAY_BOUND_C)]);\n" +
    "  int bound_sizeof_conditional = sizeof(int[TYPE_NAME_ARRAY_BOUND_CONDITION ? TYPE_NAME_ARRAY_BOUND_A : TYPE_NAME_ARRAY_BOUND_B]);\n" +
    "  int bound_sizeof_macro = sizeof(int[TYPE_NAME_ARRAY_BOUND_MACRO]);\n" +
    "  int bound_sizeof_comment = sizeof(int[/* expression gap */ TYPE_NAME_ARRAY_BOUND_A]);\n" +
    "  int bound_sizeof_splice_lf = sizeof(int[\\\n" +
    "TYPE_NAME_ARRAY_BOUND_B]);\n" +
    "  int bound_sizeof_splice_crlf = sizeof(int[\\\r\n" +
    "TYPE_NAME_ARRAY_BOUND_C]);\r\n" +
    "  int bound_alignof = _Alignof(int[TYPE_NAME_ARRAY_BOUND_A]);\n" +
    "  void *bound_cast = (int (*)[TYPE_NAME_ARRAY_BOUND_B])0;\n" +
    "  int *bound_compound = (int[TYPE_NAME_ARRAY_BOUND_C]){ 0 };\n" +
    "  int bound_cast_postfix = (*(int (*)[TYPE_NAME_ARRAY_BOUND_B])&type_name_array_bound_values)[0];\n" +
    "  int bound_compound_postfix = (int[TYPE_NAME_ARRAY_BOUND_C]){ 1 }[0];\n" +
    "  int bound_generic = _Generic(&type_name_array_bound_values, int (*)[TYPE_NAME_ARRAY_BOUND_A]: 1, default: 0);\n" +
    "  int bound_local = sizeof(int[TYPE_NAME_ARRAY_BOUND_LOCAL]);\n" +
    "  int bound_after = bound_before;\n" +
    "  return bound_sizeof_direct + bound_sizeof_unary + bound_sizeof_binary +\n" +
    "         bound_sizeof_grouped + bound_sizeof_conditional +\n" +
    "         bound_sizeof_macro + bound_sizeof_comment +\n" +
    "         bound_sizeof_splice_lf + bound_sizeof_splice_crlf +\n" +
    "         bound_alignof + bound_cast_postfix + bound_compound_postfix +\n" +
    "         bound_generic + bound_local + bound_after;\n" +
    "}\n",
};
const typeNameArrayBoundOperandCases = [
  ["type_name_array_bound_file = sizeof(int[TYPE_NAME_ARRAY_BOUND_A])",
    "TYPE_NAME_ARRAY_BOUND_A", "enumConstant", 1],
  ["bound_sizeof_direct = sizeof(int[TYPE_NAME_ARRAY_BOUND_A])",
    "TYPE_NAME_ARRAY_BOUND_A", "enumConstant", 2],
  ["sizeof(int[+TYPE_NAME_ARRAY_BOUND_A])",
    "TYPE_NAME_ARRAY_BOUND_A", "enumConstant", 0],
  ["sizeof(int[TYPE_NAME_ARRAY_BOUND_A + TYPE_NAME_ARRAY_BOUND_B])",
    "TYPE_NAME_ARRAY_BOUND_A", "enumConstant", 0],
  ["+ TYPE_NAME_ARRAY_BOUND_B])",
    "TYPE_NAME_ARRAY_BOUND_B", "enumConstant", 0],
  ["sizeof(int[(TYPE_NAME_ARRAY_BOUND_C)])",
    "TYPE_NAME_ARRAY_BOUND_C", "enumConstant", 0],
  [": TYPE_NAME_ARRAY_BOUND_B])",
    "TYPE_NAME_ARRAY_BOUND_B", "enumConstant", 0],
  ["sizeof(int[TYPE_NAME_ARRAY_BOUND_MACRO])",
    "TYPE_NAME_ARRAY_BOUND_MACRO", "macro", 0],
  ["sizeof(int[/* expression gap */ TYPE_NAME_ARRAY_BOUND_A])",
    "TYPE_NAME_ARRAY_BOUND_A", "enumConstant", 0],
  ["sizeof(int[\\\nTYPE_NAME_ARRAY_BOUND_B])",
    "TYPE_NAME_ARRAY_BOUND_B", "enumConstant", 0],
  ["sizeof(int[\\\r\nTYPE_NAME_ARRAY_BOUND_C])",
    "TYPE_NAME_ARRAY_BOUND_C", "enumConstant", 0],
  ["_Alignof(int[TYPE_NAME_ARRAY_BOUND_A])",
    "TYPE_NAME_ARRAY_BOUND_A", "enumConstant", 0],
  ["(int (*)[TYPE_NAME_ARRAY_BOUND_B])0",
    "TYPE_NAME_ARRAY_BOUND_B", "enumConstant", 0],
  ["(int[TYPE_NAME_ARRAY_BOUND_C]){ 0 }",
    "TYPE_NAME_ARRAY_BOUND_C", "enumConstant", 0],
  ["bound_cast_postfix = (*(int (*)[TYPE_NAME_ARRAY_BOUND_B])",
    "TYPE_NAME_ARRAY_BOUND_B", "enumConstant", 0],
  ["bound_compound_postfix = (int[TYPE_NAME_ARRAY_BOUND_C])",
    "TYPE_NAME_ARRAY_BOUND_C", "enumConstant", 0],
  ["int (*)[TYPE_NAME_ARRAY_BOUND_A]: 1",
    "TYPE_NAME_ARRAY_BOUND_A", "enumConstant", 0],
  ["bound_local = sizeof(int[TYPE_NAME_ARRAY_BOUND_LOCAL])",
    "TYPE_NAME_ARRAY_BOUND_LOCAL", "enumConstant", 2],
];
for (const [fragmentText, name, kind, boundaryCase] of
  typeNameArrayBoundOperandCases) {
  const fragmentIndex = typeNameArrayBoundOperandHoverSource.source.indexOf(
    fragmentText,
  );
  const useIndex = typeNameArrayBoundOperandHoverSource.source.indexOf(
    name, fragmentIndex,
  );
  assert.ok(fragmentIndex >= 0 && useIndex >= 0,
    `type-name array bound operand anchor missing for ${name}`);
  const useStart = byteOffsetForIndex(
    typeNameArrayBoundOperandHoverSource.source, useIndex,
  );
  for (const delta of [
    0, Math.floor(Buffer.byteLength(name) / 2), Buffer.byteLength(name),
  ]) {
    const byteOffset = useStart + delta;
    const result = compiler.analyzeSource(
      typeNameArrayBoundOperandHoverSource,
      {
        cursor: {
          sourceName: typeNameArrayBoundOperandHoverSource.name,
          byteOffset,
        },
      },
    );
    const completion = symbol(result, name, kind);
    assert.equal(result.partial, false,
      `${name} type-name array bound operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `${name} type-name array bound operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `${name} type-name array bound operand hover`);
    assert.equal(result.hover?.kind, kind,
      `${name} type-name array bound operand kind`);
    assert.deepStrictEqual(result.hover?.declaration, completion?.declaration,
      `${name} type-name array bound operand declaration`);
    if (kind === "enumConstant")
      assert.ok(["2", "3", "4", "6"].includes(
        result.hover?.initializer.constantValue,
      ));
    if (kind === "macro") {
      assert.equal(result.hover?.macro?.replacement, "5");
      assert.equal(result.hover?.documentation,
        "type-name array bound macro documentation");
    }
    if (boundaryCase === 1)
      assert.equal(symbol(result, "type_name_array_bound_later", "object"),
        undefined, "later type-name array bound declarator remains invisible");
    if (boundaryCase === 2) {
      assert.ok(symbol(result, "bound_parameter", "parameter"));
      assert.ok(symbol(result, "bound_before", "object"));
      assert.equal(symbol(result, "bound_after", "object"), undefined,
        "type-name array bound preserves cursor lookup point");
    }
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--type-name-array-bound-operand-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      )),
      `native and Wasm type-name array bound operand differ for ${name} at ${delta}`,
    );
  }
}

const freshTypeNameArrayBoundCompiler = await createCompiler(wasmModule);
try {
  for (const [fragmentText, name, kind] of [
    ["type_name_array_bound_file = sizeof(int[TYPE_NAME_ARRAY_BOUND_A])",
      "TYPE_NAME_ARRAY_BOUND_A", "enumConstant"],
    ["bound_local = sizeof(int[TYPE_NAME_ARRAY_BOUND_LOCAL])",
      "TYPE_NAME_ARRAY_BOUND_LOCAL", "enumConstant"],
  ]) {
    const fragmentIndex = typeNameArrayBoundOperandHoverSource.source.indexOf(
      fragmentText,
    );
    const useIndex = typeNameArrayBoundOperandHoverSource.source.indexOf(
      name, fragmentIndex,
    );
    const result = freshTypeNameArrayBoundCompiler.analyzeSource(
      typeNameArrayBoundOperandHoverSource,
      {
        cursor: {
          sourceName: typeNameArrayBoundOperandHoverSource.name,
          byteOffset: byteOffsetForIndex(
            typeNameArrayBoundOperandHoverSource.source, useIndex,
          ) + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, false,
      `fresh ${name} type-name array bound operand unexpectedly partial`);
    assert.deepStrictEqual(result.diagnostics, [],
      `fresh ${name} type-name array bound operand diagnostics`);
    assert.equal(result.hover?.name, name,
      `fresh ${name} type-name array bound operand hover`);
    assert.equal(result.hover?.kind, kind,
      `fresh ${name} type-name array bound operand kind`);
  }
} finally {
  freshTypeNameArrayBoundCompiler.dispose();
}

for (const [sourceText, name] of [
  ["enum { ZERO_BOUND = 0 }; int f(void) { return sizeof(int[ZERO_BOUND]); }\n",
    "ZERO_BOUND"],
  ["enum { INCOMPLETE_BOUND = 2 }; int f(void) { return sizeof(int[INCOMPLETE_BOUND",
    "INCOMPLETE_BOUND"],
]) {
  const useIndex = sourceText.lastIndexOf(name);
  try {
    const result = compiler.analyzeSource(
      { name: "invalid-type-name-array-bound.c", source: sourceText },
      {
        cursor: {
          sourceName: "invalid-type-name-array-bound.c",
          byteOffset: useIndex + Math.floor(Buffer.byteLength(name) / 2),
        },
      },
    );
    assert.equal(result.partial, true,
      `${name} invalid type-name array bound unexpectedly complete`);
    assert.ok(result.diagnostics.length > 0,
      `${name} invalid type-name array bound omitted diagnostics`);
  } catch (error) {
    assert.equal(error.code, "AGC_LANGUAGE_ANALYSIS_FAILED",
      `${name} invalid type-name array bound failure code`);
    assert.ok(error.diagnostics?.length > 0,
      `${name} invalid type-name array bound failure diagnostics`);
  }
}
const reusedTypeNameArrayBoundFragment =
  typeNameArrayBoundOperandHoverSource.source.indexOf(
    "bound_sizeof_direct = sizeof(int[TYPE_NAME_ARRAY_BOUND_A])",
  );
const reusedTypeNameArrayBoundUse =
  typeNameArrayBoundOperandHoverSource.source.indexOf(
    "TYPE_NAME_ARRAY_BOUND_A", reusedTypeNameArrayBoundFragment,
  );
const reusedTypeNameArrayBoundResult = compiler.analyzeSource(
  typeNameArrayBoundOperandHoverSource,
  {
    cursor: {
      sourceName: typeNameArrayBoundOperandHoverSource.name,
      byteOffset: byteOffsetForIndex(
        typeNameArrayBoundOperandHoverSource.source,
        reusedTypeNameArrayBoundUse,
      ) + 3,
    },
  },
);
assert.equal(reusedTypeNameArrayBoundResult.partial, false,
  "type-name array bound compiler remained partial after invalid source");
assert.deepStrictEqual(reusedTypeNameArrayBoundResult.diagnostics, [],
  "type-name array bound compiler retained invalid diagnostics");
assert.equal(reusedTypeNameArrayBoundResult.hover?.name,
  "TYPE_NAME_ARRAY_BOUND_A",
  "type-name array bound compiler was not reusable after invalid source");

const snakeCastFragment = "(unsigned int)MAX_SNAKE_LENGTH";
const snakeCastFragmentIndex = macroDefinitionSnake.source.indexOf(
  snakeCastFragment,
);
const snakeCastIndex = macroDefinitionSnake.source.indexOf(
  "MAX_SNAKE_LENGTH", snakeCastFragmentIndex,
);
assert.ok(snakeCastFragmentIndex >= 0 && snakeCastIndex >= 0,
  "snake cast operand anchor missing");
const snakeCastStart = byteOffsetForIndex(
  macroDefinitionSnake.source, snakeCastIndex,
);
let snakeCastResult;
for (const delta of [0, Math.floor("MAX_SNAKE_LENGTH".length / 2),
  "MAX_SNAKE_LENGTH".length]) {
  const byteOffset = snakeCastStart + delta;
  snakeCastResult = compiler.analyzeSource(macroDefinitionSnake, {
    headers: { "game.h": macroDefinitionGameHeader },
    cursor: { sourceName: macroDefinitionSnake.name, byteOffset },
  });
  assertMacroDefinitionSnapshot(snakeCastResult, {
    name: "MAX_SNAKE_LENGTH",
    replacement: "( BOARD_COLUMNS * BOARD_ROWS )",
    documentation: "盤面に収まるヘビの最大の長さです。",
    parameters: [],
  }, `snake cast operand at ${delta}`);
  assert.equal(snakeCastResult.partial, false);
  assert.deepStrictEqual(snakeCastResult.diagnostics, []);
  assert.deepStrictEqual(
    snakeCastResult,
    JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--macro-definition-snake-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    )),
    `native and Wasm snake cast operand differ at ${delta}`,
  );
}
const snakeOrdinaryIndex = macroDefinitionSnake.source.lastIndexOf(
  "MAX_SNAKE_LENGTH",
);
const snakeOrdinaryResult = compiler.analyzeSource(macroDefinitionSnake, {
  headers: { "game.h": macroDefinitionGameHeader },
  cursor: {
    sourceName: macroDefinitionSnake.name,
    byteOffset: byteOffsetForIndex(
      macroDefinitionSnake.source, snakeOrdinaryIndex,
    ) + Math.floor("MAX_SNAKE_LENGTH".length / 2),
  },
});
assert.deepStrictEqual(
  snakeCastResult.hover.declaration, snakeOrdinaryResult.hover.declaration,
  "cast and ordinary snake macro uses resolve to different declarations",
);
assert.equal(snakeCastResult.hover.macro.replacement,
  snakeOrdinaryResult.hover.macro.replacement);
assert.equal(snakeCastResult.hover.documentation,
  snakeOrdinaryResult.hover.documentation);

const freshCastCompiler = await createCompiler(wasmModule);
try {
  const freshResult = freshCastCompiler.analyzeSource(macroDefinitionSnake, {
    headers: { "game.h": macroDefinitionGameHeader },
    cursor: {
      sourceName: macroDefinitionSnake.name,
      byteOffset: snakeCastStart + Math.floor("MAX_SNAKE_LENGTH".length / 2),
    },
  });
  assertMacroDefinitionSnapshot(freshResult, {
    name: "MAX_SNAKE_LENGTH",
    replacement: "( BOARD_COLUMNS * BOARD_ROWS )",
    parameters: [],
  }, "fresh snake cast operand");
  const name = "cast_object";
  const adjacentFragment = castOperandHoverSource.source.indexOf(
    "adjacent_builtin = (int)(long)cast_object",
  );
  const adjacentUse = castOperandHoverSource.source.indexOf(
    name, adjacentFragment,
  );
  const adjacentResult = freshCastCompiler.analyzeSource(
    castOperandHoverSource,
    {
      cursor: {
        sourceName: castOperandHoverSource.name,
        byteOffset: byteOffsetForIndex(
          castOperandHoverSource.source, adjacentUse,
        ) + Math.floor(Buffer.byteLength(name) / 2),
      },
    },
  );
  assert.equal(adjacentResult.partial, false,
    "fresh adjacent cast operand unexpectedly partial");
  assert.deepStrictEqual(adjacentResult.diagnostics, [],
    "fresh adjacent cast operand diagnostics");
  assert.equal(adjacentResult.hover?.name, name,
    "fresh adjacent cast operand hover");
  assert.equal(adjacentResult.hover?.kind, "object",
    "fresh adjacent cast operand kind");
  assert.deepStrictEqual(
    adjacentResult.hover?.declaration,
    symbol(adjacentResult, name, "object")?.declaration,
    "fresh adjacent cast operand declaration",
  );
} finally {
  freshCastCompiler.dispose();
}

for (const input of [
  {
    name: "invalid-cast.c",
    source: "int target; int f(void) { return (unsigned mystery)target; }\n",
    cursorName: "target",
  },
  {
    name: "invalid-cast.c",
    source: "int f(void) { return (unsigned int); }\n",
    cursorName: null,
  },
  {
    name: "invalid-cast.c",
    source: "int target; int f(void) { return (struct)target; }\n",
    cursorName: "target",
  },
]) {
  let invalidResult = null;
  let invalidError = null;
  const cursorIndex = input.cursorName === null
    ? input.source.length
    : input.source.lastIndexOf(input.cursorName) + 3;
  try {
    invalidResult = compiler.analyzeSource(input, {
      cursor: { sourceName: input.name, byteOffset: cursorIndex },
    });
  } catch (error) {
    invalidError = error;
  }
  assert.ok(
    invalidResult
      ? invalidResult.partial && invalidResult.diagnostics.length > 0
      : invalidError?.name === "AgcLanguageAnalysisError" &&
        invalidError.diagnostics?.length > 0,
    `invalid cast lost diagnostics: ${JSON.stringify(invalidResult || invalidError)}`,
  );
}

const castProjectCompiler = await createCompiler(wasmModule);
try {
  const castProjectSources = [
    "/// project cast v1\n#define PROJECT_CAST_VALUE 31\n" +
      "int project_cast(void) { return (unsigned int)PROJECT_CAST_VALUE; }\n",
    "\n/// project cast v2\n#define PROJECT_CAST_VALUE 32\n" +
      "int project_cast(void) { return (long)PROJECT_CAST_VALUE; }\n",
  ];
  for (let revision = 1; revision <= castProjectSources.length; revision++) {
    const input = { name: "main.c", source: castProjectSources[revision - 1] };
    const useIndex = input.source.indexOf(")PROJECT_CAST_VALUE") + 1;
    const result = castProjectCompiler.analyzeProjectSource(input, {
      projectRevision: revision,
      projectSources: [input],
      cursor: {
        sourceName: input.name,
        byteOffset: byteOffsetForIndex(input.source, useIndex) + 4,
      },
    });
    assertMacroDefinitionSnapshot(result, {
      name: "PROJECT_CAST_VALUE", replacement: String(30 + revision),
      parameters: [],
    }, `cast project revision ${revision}`);
    assert.equal(result.hover.documentation, `project cast v${revision}`);
    assert.deepStrictEqual(
      result,
      JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--cast-operand-project-parity-json", String(revision)],
        { encoding: "utf8" },
      )),
      `native and Wasm cast project differ at revision ${revision}`,
    );
  }
} finally {
  castProjectCompiler.dispose();
}

const castLimitUseIndex = castOperandHoverSource.source.indexOf(
  "CAST_OPERAND_MACRO",
  castOperandHoverSource.source.indexOf("simple = (int)CAST_OPERAND_MACRO"),
);
const castLimitCursor = byteOffsetForIndex(
  castOperandHoverSource.source, castLimitUseIndex,
) + 3;
const exactCastLimitResult = compiler.analyzeSource(castOperandHoverSource, {
  cursor: {
    sourceName: castOperandHoverSource.name,
    byteOffset: castLimitCursor,
  },
  limits: { maxSourceBytes: Buffer.byteLength(castOperandHoverSource.source) },
});
assert.equal(exactCastLimitResult.hover?.name, "CAST_OPERAND_MACRO");
assert.throws(
  () => compiler.analyzeSource(castOperandHoverSource, {
    cursor: {
      sourceName: castOperandHoverSource.name,
      byteOffset: castLimitCursor,
    },
    limits: {
      maxSourceBytes: Buffer.byteLength(castOperandHoverSource.source) - 1,
    },
  }),
  (error) => error instanceof AgcResourceLimitError &&
    error.code === "AGC_LIMIT_MAX_SOURCE_BYTES",
);
function castSnapshotSucceeds(maxAnalysisSnapshotBytes) {
  try {
    compiler.analyzeSource(castOperandHoverSource, {
      cursor: {
        sourceName: castOperandHoverSource.name,
        byteOffset: castLimitCursor,
      },
      limits: { maxAnalysisSnapshotBytes },
    });
    return true;
  } catch (error) {
    if (!(error instanceof AgcResourceLimitError) ||
        error.code !== "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES") throw error;
    return false;
  }
}
let castSnapshotLow = 1;
let castSnapshotHigh = 1024 * 1024;
assert.equal(castSnapshotSucceeds(castSnapshotHigh), true);
while (castSnapshotLow < castSnapshotHigh) {
  const middle = castSnapshotLow +
    Math.floor((castSnapshotHigh - castSnapshotLow) / 2);
  if (castSnapshotSucceeds(middle)) castSnapshotHigh = middle;
  else castSnapshotLow = middle + 1;
}
assert.equal(castSnapshotSucceeds(castSnapshotLow - 1), false);
assert.equal(castSnapshotSucceeds(castSnapshotLow), true,
  "Wasm session was not reusable after cast snapshot limit");

const snakeEnumCases = [
  {
    name: "Direction", kind: "tag",
    documentation: "ヘビが進む方向を表します。",
    comment: "/// ヘビが進む方向を表します。",
  },
  {
    name: "DIRECTION_LEFT", kind: "enumConstant",
    documentation: "左へ進む方向です。", comment: "/// 左へ進む方向です。",
  },
  {
    name: "DIRECTION_RIGHT", kind: "enumConstant",
    documentation: "", comment: null,
  },
  {
    name: "DIRECTION_UP", kind: "enumConstant",
    documentation: "上へ進む方向です。", comment: "/** 上へ進む方向です。 */",
  },
  {
    name: "DIRECTION_DOWN", kind: "enumConstant",
    documentation: "下へ進む方向です。", comment: "/// 下へ進む方向です。",
  },
];
for (const enumCase of snakeEnumCases) {
  const declarationIndex = macroDefinitionSnake.source.indexOf(enumCase.name);
  const useIndex = macroDefinitionSnake.source.lastIndexOf(enumCase.name);
  const commentIndex = enumCase.comment === null ? -1
    : macroDefinitionSnake.source.indexOf(enumCase.comment);
  for (const [lifecycle, index] of [
    ["declaration", declarationIndex], ["use", useIndex],
  ]) {
    const byteOffset = byteOffsetForIndex(macroDefinitionSnake.source, index) +
      Math.floor(Buffer.byteLength(enumCase.name) / 2);
    const result = compiler.analyzeSource(macroDefinitionSnake, {
      headers: { "game.h": macroDefinitionGameHeader },
      cursor: { sourceName: macroDefinitionSnake.name, byteOffset },
    });
    const completion = symbol(result, enumCase.name, enumCase.kind);
    assert.equal(result.hover?.name, enumCase.name,
      `${enumCase.name} snake ${lifecycle} hover`);
    assert.equal(result.hover?.kind, enumCase.kind,
      `${enumCase.name} snake ${lifecycle} kind`);
    assert.equal(result.hover?.documentation, enumCase.documentation,
      `${enumCase.name} snake ${lifecycle} documentation`);
    assert.equal(completion?.documentation, enumCase.documentation,
      `${enumCase.name} snake ${lifecycle} completion documentation`);
    if (enumCase.comment === null) {
      assert.equal(result.hover?.documentationRange, null,
        `${enumCase.name} snake ${lifecycle} range`);
      assert.equal(completion?.documentationRange, null,
        `${enumCase.name} snake ${lifecycle} completion range`);
    } else {
      const commentStart = byteOffsetForIndex(
        macroDefinitionSnake.source, commentIndex,
      );
      assert.equal(result.hover?.documentationRange?.sourceName, "snake.c");
      assert.equal(result.hover?.documentationRange?.start.offset, commentStart);
      assert.equal(
        result.hover?.documentationRange?.end.offset,
        commentStart + Buffer.byteLength(enumCase.comment),
      );
      assert.deepStrictEqual(
        result.hover?.documentationRange, completion?.documentationRange,
      );
    }
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--macro-definition-snake-parity-json", String(byteOffset)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      result,
      nativeResult,
      `native and Wasm snake enum ${lifecycle} differ for ${enumCase.name}`,
    );
  }
}

const macroHeaderDefinitionSource = {
  name: "macro-definition.h",
  source: "/// virtual header definition\n" +
    "#define HEADER_DEFINITION(value) ((value) + 2)\n",
};
const macroHeaderDefinitionResult = compiler.analyzeSource(
  macroHeaderDefinitionSource,
  {
    cursor: {
      sourceName: macroHeaderDefinitionSource.name,
      byteOffset: macroHeaderDefinitionSource.source.indexOf(
        "HEADER_DEFINITION",
      ) + 3,
    },
  },
);
assertMacroDefinitionSnapshot(macroHeaderDefinitionResult, {
  name: "HEADER_DEFINITION", replacement: "( ( value ) + 2 )",
  parameters: ["value"],
}, "virtual header definition");
assert.equal(
  macroHeaderDefinitionResult.hover.declaration.sourceName,
  macroHeaderDefinitionSource.name,
);
assert.deepStrictEqual(
  macroHeaderDefinitionResult,
  JSON.parse(execFileSync(
    nativeAnalysisPath,
    ["--macro-definition-header-parity-json"],
    { encoding: "utf8" },
  )),
  "native and Wasm virtual header macro definition differ",
);

const macroDefinitionProjectCompiler = await createCompiler(wasmModule);
try {
  const projectSources = [
    "/// project definition v1\n#define PROJECT_DEFINITION 21\n" +
      "int project_value(void) { return PROJECT_DEFINITION; }\n",
    "\n/// project definition v2\n#define PROJECT_DEFINITION 22\n" +
      "int project_value(void) { return PROJECT_DEFINITION; }\n",
  ];
  for (let revision = 1; revision <= projectSources.length; revision++) {
    const input = { name: "main.c", source: projectSources[revision - 1] };
    const result = macroDefinitionProjectCompiler.analyzeProjectSource(input, {
      projectRevision: revision,
      projectSources: [input],
      cursor: {
        sourceName: input.name,
        byteOffset: input.source.indexOf("PROJECT_DEFINITION") + 4,
      },
    });
    assertMacroDefinitionSnapshot(result, {
      name: "PROJECT_DEFINITION", replacement: String(20 + revision),
      parameters: [],
    }, `project macro definition revision ${revision}`);
    assert.equal(
      result.hover.documentation,
      `project definition v${revision}`,
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--macro-definition-project-parity-json", String(revision)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      result,
      nativeResult,
      `native and Wasm project macro definition differ at revision ${revision}`,
    );
  }
} finally {
  macroDefinitionProjectCompiler.dispose();
}

const enumHeaderMain = {
  name: "enum-header-main.c",
  source: "#include \"enum-doc.h\"\n" +
    "int read_header_direction(enum HeaderDirection direction) {\n" +
    "  return direction == HEADER_DIRECTION_VALUE;\n" +
    "}\n",
};
const enumHeaderRevisions = [
  "/// header enum v1\n" +
    "enum HeaderDirection {\n" +
    "  /// header constant v1\n" +
    "  HEADER_DIRECTION_VALUE = 1\n" +
    "};\n",
  "/** header enum v2 */\n" +
    "enum HeaderDirection {\n" +
    "  /** header constant v2 */\n" +
    "  HEADER_DIRECTION_VALUE = 2\n" +
    "};\n",
  "enum HeaderDirection { HEADER_DIRECTION_VALUE = 3 };\n",
];
const enumHeaderTagUse = enumHeaderMain.source.lastIndexOf("HeaderDirection");
const enumHeaderValueUse = enumHeaderMain.source.lastIndexOf(
  "HEADER_DIRECTION_VALUE",
);
for (let revision = 1; revision <= enumHeaderRevisions.length; revision++) {
  const header = enumHeaderRevisions[revision - 1];
  const expectedTag = revision === 1 ? "header enum v1"
    : revision === 2 ? "header enum v2" : "";
  const expectedValue = revision === 1 ? "header constant v1"
    : revision === 2 ? "header constant v2" : "";
  const options = {
    headers: { "enum-doc.h": header },
    cursor: {
      sourceName: enumHeaderMain.name,
      byteOffset: enumHeaderValueUse + 3,
    },
  };
  const result = compiler.analyzeSource(enumHeaderMain, options);
  const tag = symbol(result, "HeaderDirection", "tag");
  const value = symbol(result, "HEADER_DIRECTION_VALUE", "enumConstant");
  assert.equal(result.hover?.name, "HEADER_DIRECTION_VALUE");
  assert.equal(result.hover?.documentation, expectedValue);
  assert.equal(value?.documentation, expectedValue);
  assert.equal(tag?.documentation, expectedTag);
  for (const documented of [tag, value]) {
    assert.equal(
      documented?.documentationRange?.sourceName ?? null,
      documented?.documentation ? "enum-doc.h" : null,
    );
  }
  const nativeResult = JSON.parse(execFileSync(
    nativeAnalysisPath,
    ["--enum-documentation-header-parity-json", String(revision)],
    { encoding: "utf8" },
  ));
  assert.deepStrictEqual(
    result,
    nativeResult,
    `native and Wasm header enum documentation differ at revision ${revision}`,
  );
  const tagResult = compiler.analyzeSource(enumHeaderMain, {
    ...options,
    cursor: {
      sourceName: enumHeaderMain.name,
      byteOffset: enumHeaderTagUse + 2,
    },
  });
  assert.equal(tagResult.hover?.name, "HeaderDirection");
  assert.equal(tagResult.hover?.documentation, expectedTag);
}

const enumProjectRevisions = [
  "/// project enum v1\n" +
    "enum ProjectDirection {\n" +
    "  /// project constant v1\n" +
    "  PROJECT_DIRECTION_VALUE = 1\n" +
    "};\n" +
    "int read_project_direction(enum ProjectDirection direction) {\n" +
    "  return direction == PROJECT_DIRECTION_VALUE;\n" +
    "}\n",
  "/** project enum v2 */\n" +
    "enum ProjectDirection {\n" +
    "  /** project constant v2 */\n" +
    "  PROJECT_DIRECTION_VALUE = 2\n" +
    "};\n" +
    "int read_project_direction(enum ProjectDirection direction) {\n" +
    "  return direction == PROJECT_DIRECTION_VALUE;\n" +
    "}\n",
  "enum ProjectDirection { PROJECT_DIRECTION_VALUE = 3 };\n" +
    "int read_project_direction(enum ProjectDirection direction) {\n" +
    "  return direction == PROJECT_DIRECTION_VALUE;\n" +
    "}\n",
];
const enumProjectCompiler = await createCompiler(wasmModule);
try {
  for (let revision = 1; revision <= enumProjectRevisions.length; revision++) {
    const input = {
      name: "main.c", source: enumProjectRevisions[revision - 1],
    };
    const valueUse = input.source.lastIndexOf("PROJECT_DIRECTION_VALUE");
    const expectedTag = revision === 1 ? "project enum v1"
      : revision === 2 ? "project enum v2" : "";
    const expectedValue = revision === 1 ? "project constant v1"
      : revision === 2 ? "project constant v2" : "";
    const result = enumProjectCompiler.analyzeProjectSource(input, {
      projectRevision: revision,
      projectSources: [input],
      cursor: {
        sourceName: input.name,
        byteOffset: valueUse + 3,
      },
    });
    assert.equal(result.hover?.name, "PROJECT_DIRECTION_VALUE");
    assert.equal(result.hover?.documentation, expectedValue);
    assert.equal(
      symbol(result, "PROJECT_DIRECTION_VALUE", "enumConstant")
        ?.documentation,
      expectedValue,
    );
    assert.equal(
      symbol(result, "ProjectDirection", "tag")?.documentation,
      expectedTag,
    );
    const nativeResult = JSON.parse(execFileSync(
      nativeAnalysisPath,
      ["--enum-documentation-project-parity-json", String(revision)],
      { encoding: "utf8" },
    ));
    assert.deepStrictEqual(
      result,
      nativeResult,
      `native and Wasm project enum documentation differ at revision ${revision}`,
    );
    const tagUse = input.source.lastIndexOf("ProjectDirection");
    const tagResult = enumProjectCompiler.analyzeProjectSource(input, {
      projectRevision: revision,
      projectSources: [input],
      cursor: { sourceName: input.name, byteOffset: tagUse + 2 },
    });
    assert.equal(tagResult.hover?.name, "ProjectDirection");
    assert.equal(tagResult.hover?.documentation, expectedTag);
  }
} finally {
  enumProjectCompiler.dispose();
}

const enumScopeSource = {
  name: "enum-scope.c",
  source: "/** outer enum */\n" +
    "enum ScopedDirection {\n" +
    "  /** outer value */\n" +
    "  SCOPED_DIRECTION_VALUE = 1\n" +
    "};\n" +
    "int read_inner_direction(void) {\n" +
    "  /** inner enum */\n" +
    "  enum ScopedDirection {\n" +
    "    /** inner value */\n" +
    "    SCOPED_DIRECTION_VALUE = 2\n" +
    "  };\n" +
    "  enum ScopedDirection value = SCOPED_DIRECTION_VALUE;\n" +
    "  return value;\n" +
    "}\n" +
    "enum ScopedDirection outer_direction = SCOPED_DIRECTION_VALUE;\n",
};
const innerScopeAnchor = enumScopeSource.source.indexOf("inner enum");
const innerTagUse = enumScopeSource.source.indexOf(
  "ScopedDirection", enumScopeSource.source.indexOf("enum ScopedDirection value"),
);
const innerValueUse = enumScopeSource.source.indexOf(
  "SCOPED_DIRECTION_VALUE", innerTagUse,
);
const enumScopeCases = [
  { index: innerTagUse, name: "ScopedDirection", documentation: "inner enum" },
  { index: innerValueUse, name: "SCOPED_DIRECTION_VALUE",
    documentation: "inner value" },
  { index: enumScopeSource.source.lastIndexOf("ScopedDirection"),
    name: "ScopedDirection", documentation: "outer enum" },
  { index: enumScopeSource.source.lastIndexOf("SCOPED_DIRECTION_VALUE"),
    name: "SCOPED_DIRECTION_VALUE", documentation: "outer value" },
];
assert.notEqual(innerScopeAnchor, -1);
for (const scopeCase of enumScopeCases) {
  const byteOffset = scopeCase.index + 2;
  const result = compiler.analyzeSource(enumScopeSource, {
    cursor: { sourceName: enumScopeSource.name, byteOffset },
  });
  assert.equal(result.hover?.name, scopeCase.name);
  assert.equal(result.hover?.documentation, scopeCase.documentation);
  assert.deepStrictEqual(result, JSON.parse(execFileSync(
    nativeAnalysisPath,
    ["--enum-documentation-scope-parity-json", String(byteOffset)],
    { encoding: "utf8" },
  )), `native and Wasm nested enum differ for ${scopeCase.documentation}`);
}

const limitedEnumSource = {
  name: "enum-limit.c", source: "/** 12345678901234 */\nenum E { V };\n",
};
try {
  compiler.analyzeSource(limitedEnumSource, {
    cursor: {
      sourceName: limitedEnumSource.name,
      byteOffset: limitedEnumSource.source.indexOf(" E ") + 1,
    },
    limits: { maxAnalysisStringBytes: 13 },
  });
  throw new Error("enum documentation string limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES" ||
      error.limit !== "maxAnalysisStringBytes" || error.max !== 13 ||
      error.actual !== 14) {
    throw error;
  }
}
const enumEntryLimitSource = {
  name: "enum-entry-limit.c",
  source: "/** first */\nenum A { A_VALUE };\n" +
    "/** second */\nenum B { B_VALUE };\n",
};
try {
  compiler.analyzeSource(enumEntryLimitSource, {
    cursor: {
      sourceName: enumEntryLimitSource.name,
      byteOffset: Buffer.byteLength(enumEntryLimitSource.source),
    },
    limits: { maxAnalysisSymbols: 1 },
  });
  throw new Error("enum documentation entry limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS" ||
      error.limit !== "maxAnalysisSymbols" || error.max !== 1 ||
      error.actual !== 2) {
    throw error;
  }
}
const plainEnumSnapshotInput = {
  name: "enum-snapshot.c", source: "enum S { S_VALUE };\n",
};
const documentedEnumSnapshotInput = {
  name: "enum-snapshot.c",
  source: "/** bounded enum doc */\nenum S { S_VALUE };\n",
};
const plainEnumSnapshotMinimum = minimumDocumentationSnapshotLimit(
  plainEnumSnapshotInput,
);
const documentedEnumSnapshotMinimum = minimumDocumentationSnapshotLimit(
  documentedEnumSnapshotInput,
);
assert.ok(documentedEnumSnapshotMinimum > plainEnumSnapshotMinimum,
  "enum documentation did not contribute to the Wasm snapshot byte limit");
assert.equal(documentationSnapshotSucceeds(
  plainEnumSnapshotInput, documentedEnumSnapshotMinimum - 1,
), true);
assert.equal(documentationSnapshotSucceeds(
  documentedEnumSnapshotInput, documentedEnumSnapshotMinimum - 1,
), false);
assert.equal(documentationSnapshotSucceeds(
  plainEnumSnapshotInput, documentedEnumSnapshotMinimum - 1,
), true, "Wasm session was not reusable after enum documentation limits");

const macroDefinitionLimitSource = {
  name: "macro-definition-limit.c",
  source: "#define LIMIT_DEFINITION 1\nint unfinished(",
};
const macroDefinitionLimitCursor =
  macroDefinitionLimitSource.source.indexOf("LIMIT_DEFINITION") + 2;
const exactSourceLimitResult = compiler.analyzeSource(
  macroDefinitionLimitSource,
  {
    cursor: {
      sourceName: macroDefinitionLimitSource.name,
      byteOffset: macroDefinitionLimitCursor,
    },
    limits: {
      maxSourceBytes: Buffer.byteLength(macroDefinitionLimitSource.source),
    },
  },
);
assertMacroDefinitionSnapshot(exactSourceLimitResult, {
  name: "LIMIT_DEFINITION", replacement: "1", parameters: [],
}, "exact macro definition source limit");
function macroDefinitionSnapshotSucceeds(maxAnalysisSnapshotBytes) {
  try {
    compiler.analyzeSource(macroDefinitionLimitSource, {
      cursor: {
        sourceName: macroDefinitionLimitSource.name,
        byteOffset: macroDefinitionLimitCursor,
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
let macroDefinitionSnapshotLow = 1;
let macroDefinitionSnapshotHigh = 64 * 1024;
assert.equal(macroDefinitionSnapshotSucceeds(macroDefinitionSnapshotHigh), true);
while (macroDefinitionSnapshotLow < macroDefinitionSnapshotHigh) {
  const middle = macroDefinitionSnapshotLow + Math.floor(
    (macroDefinitionSnapshotHigh - macroDefinitionSnapshotLow) / 2,
  );
  if (macroDefinitionSnapshotSucceeds(middle)) {
    macroDefinitionSnapshotHigh = middle;
  } else {
    macroDefinitionSnapshotLow = middle + 1;
  }
}
assert.equal(
  macroDefinitionSnapshotSucceeds(macroDefinitionSnapshotLow - 1),
  false,
);
try {
  compiler.analyzeSource(macroDefinitionLimitSource, {
    cursor: {
      sourceName: macroDefinitionLimitSource.name,
      byteOffset: macroDefinitionLimitCursor,
    },
    limits: {
      maxSourceBytes: Buffer.byteLength(macroDefinitionLimitSource.source) - 1,
    },
  });
  throw new Error("macro definition source limit unexpectedly succeeded");
} catch (error) {
  if (!(error instanceof AgcResourceLimitError) ||
      error.code !== "AGC_LIMIT_MAX_SOURCE_BYTES" ||
      error.limit !== "maxSourceBytes") {
    throw error;
  }
}
const macroDefinitionAfterLimit = compiler.analyzeSource(
  { name: "macro-definition-limit.c", source: "#define SAFE_MACRO 1\n" },
  {
    cursor: {
      sourceName: "macro-definition-limit.c",
      byteOffset: Buffer.byteLength("#define SAFE"),
    },
  },
);
assertMacroDefinitionSnapshot(macroDefinitionAfterLimit, {
  name: "SAFE_MACRO", replacement: "1", parameters: [],
}, "macro definition reuse after limit");

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
    "int GENERIC_INVOKED;\n" +
    "#define GENERIC_INVOKED() 7\n" +
    "??=define GENERIC_TRIGRAPH_PREFIX 11\n" +
    "#define GENERIC_SPLICE_PREFIX 12 \\\n" +
    "+ 0\n" +
    "#define GENERIC_AFTER_TRANSLATION 13\n" +
    "int generic_after_translation;\n" +
    "int generic_literal_lf_\\\n" +
    "value;\n" +
    "int generic_literal_crlf_\\\r\n" +
    "value;\n" +
    "int generic_trigraph_lf_??/\n" +
    "value;\n" +
    "int generic_trigraph_crlf_??/\r\n" +
    "value;\n" +
    "int generic_identity(int value) { return value; }\n" +
    "int generic_comment_invocation(void) { return GENERIC_INVOKED /* gap */ (); }\n" +
    "int generic_lf_invocation(void) { return GENERIC_INVOKED \\\n" +
    "(); }\n" +
    "int generic_crlf_invocation(void) { return GENERIC_INVOKED \\\r\n" +
    "(); }\n" +
    "int generic_trigraph_lf_invocation(void) { return GENERIC_INVOKED ??/\n" +
    "(); }\n" +
    "int generic_trigraph_crlf_invocation(void) { return GENERIC_INVOKED ??/\r\n" +
    "(); }\n" +
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
    "  result += GENERIC_TRIGRAPH_PREFIX + GENERIC_AFTER_TRANSLATION + generic_after_translation;\n" +
    "  result += generic_literal_lf_\\\n" +
    "value;\n" +
    "  result += generic_literal_crlf_\\\r\n" +
    "value;\n" +
    "  result += generic_trigraph_lf_??/\n" +
    "value;\n" +
    "  result += generic_trigraph_crlf_??/\r\n" +
    "value;\n" +
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
const genericInvokedObjectDeclaration = findGenericName("GENERIC_INVOKED");
const genericInvokedMacroDeclaration = findGenericName(
  "GENERIC_INVOKED",
  genericInvokedObjectDeclaration + "GENERIC_INVOKED".length,
);
const genericTrigraphPrefixDeclaration = findGenericName(
  "GENERIC_TRIGRAPH_PREFIX", genericInvokedMacroDeclaration,
);
const genericAfterTranslationMacroDeclaration = findGenericName(
  "GENERIC_AFTER_TRANSLATION", genericTrigraphPrefixDeclaration,
);
const genericAfterTranslationObjectDeclaration = findGenericName(
  "generic_after_translation", genericAfterTranslationMacroDeclaration,
);
const genericTypedefDeclaration = findGenericName("GenericScore");
const genericStructDeclaration = findGenericName("GenericPlayer");
const genericUnionDeclaration = findGenericName("GenericPayload");
const genericEnumTagDeclaration = findGenericName("GenericState");
const genericEnumDeclaration = findGenericName("GENERIC_MODE");
const genericObjectDeclaration = findGenericName("generic_value");
const genericFunctionDeclaration = findGenericName("generic_identity");
const genericCommentInvocation = findGenericName(
  "generic_comment_invocation", genericFunctionDeclaration,
);
const genericCommentMacroUse = findGenericName(
  "GENERIC_INVOKED", genericCommentInvocation,
);
const genericLfInvocation = findGenericName(
  "generic_lf_invocation", genericCommentMacroUse,
);
const genericLfMacroUse = findGenericName(
  "GENERIC_INVOKED", genericLfInvocation,
);
const genericCrlfInvocation = findGenericName(
  "generic_crlf_invocation", genericLfMacroUse,
);
const genericCrlfMacroUse = findGenericName(
  "GENERIC_INVOKED", genericCrlfInvocation,
);
const genericTrigraphLfInvocation = findGenericName(
  "generic_trigraph_lf_invocation", genericCrlfMacroUse,
);
const genericTrigraphLfMacroUse = findGenericName(
  "GENERIC_INVOKED", genericTrigraphLfInvocation,
);
const genericTrigraphCrlfInvocation = findGenericName(
  "generic_trigraph_crlf_invocation", genericTrigraphLfMacroUse,
);
const genericTrigraphCrlfMacroUse = findGenericName(
  "GENERIC_INVOKED", genericTrigraphCrlfInvocation,
);
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
const genericTrigraphPrefixUse = findGenericName(
  "GENERIC_TRIGRAPH_PREFIX", genericCallMacroUse,
);
const genericAfterTranslationMacroUse = findGenericName(
  "GENERIC_AFTER_TRANSLATION", genericTrigraphPrefixUse,
);
const genericAfterTranslationObjectUse = findGenericName(
  "generic_after_translation", genericAfterTranslationMacroUse,
);
const genericTypedefControl = findGenericName(
  "_Generic(generic_value, GenericScore", genericAfterTranslationObjectUse,
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
  { name: "GENERIC_TRIGRAPH_PREFIX", kind: "macro", replacement: "11",
    index: genericTrigraphPrefixUse },
  { name: "GENERIC_AFTER_TRANSLATION", kind: "macro", replacement: "13",
    index: genericAfterTranslationMacroUse },
  { name: "generic_after_translation", kind: "object",
    index: genericAfterTranslationObjectUse },
  { name: "GENERIC_INVOKED", kind: "macro", replacement: "7",
    index: genericCommentMacroUse },
  { name: "GENERIC_INVOKED", kind: "macro", replacement: "7",
    index: genericLfMacroUse },
  { name: "GENERIC_INVOKED", kind: "macro", replacement: "7",
    index: genericCrlfMacroUse },
  { name: "GENERIC_INVOKED", kind: "macro", replacement: "7",
    index: genericTrigraphLfMacroUse },
  { name: "GENERIC_INVOKED", kind: "macro", replacement: "7",
    index: genericTrigraphCrlfMacroUse },
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
  ["GENERIC_INVOKED", genericInvokedMacroDeclaration],
  ["GENERIC_TRIGRAPH_PREFIX", genericTrigraphPrefixDeclaration],
  ["GENERIC_AFTER_TRANSLATION", genericAfterTranslationMacroDeclaration],
  ["generic_after_translation", genericAfterTranslationObjectDeclaration],
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

function genericPositionAt(byteOffset) {
  const bytes = Buffer.from(genericSource.source);
  let line = 1;
  let column = 1;
  for (let index = 0; index < byteOffset; index++) {
    if (bytes[index] === 0x0a) {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  return { line, column, offset: byteOffset };
}

function genericPositionEquals(actual, expected) {
  return actual?.line === expected.line &&
    actual.column === expected.column && actual.offset === expected.offset;
}

function assertGenericHover(result, analysisCase, lifecycle) {
  const hover = result.hover;
  const declarationStart = genericDeclarations.get(analysisCase.name);
  const declarationEnd =
    declarationStart + Buffer.byteLength(analysisCase.name);
  const expectedStart = genericPositionAt(declarationStart);
  const expectedEnd = genericPositionAt(declarationEnd);
  if (result.partial || result.diagnostics.length !== 0 ||
      hover?.name !== analysisCase.name || hover.kind !== analysisCase.kind ||
      hover.declaration.sourceName !== genericSource.name ||
      !genericPositionEquals(hover.declaration.start, expectedStart) ||
      !genericPositionEquals(hover.declaration.end, expectedEnd) ||
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

const genericSplicedIdentifierCases = [
  { name: "generic_literal_lf_value",
    spelling: "generic_literal_lf_\\\nvalue" },
  { name: "generic_literal_crlf_value",
    spelling: "generic_literal_crlf_\\\r\nvalue" },
  { name: "generic_trigraph_lf_value",
    spelling: "generic_trigraph_lf_??/\nvalue" },
  { name: "generic_trigraph_crlf_value",
    spelling: "generic_trigraph_crlf_??/\r\nvalue" },
].map((analysisCase) => {
  const declaration = genericSource.source.indexOf(analysisCase.spelling);
  const use = genericSource.source.indexOf(
    analysisCase.spelling, declaration + analysisCase.spelling.length,
  );
  let splice = analysisCase.spelling.indexOf("\\");
  if (splice < 0) splice = analysisCase.spelling.indexOf("??/");
  const newline = analysisCase.spelling.indexOf("\n", splice);
  if (declaration < 0 || use < 0 || splice < 0 || newline < 0) {
    throw new Error(`spliced identifier anchors missing: ${analysisCase.name}`);
  }
  return {
    ...analysisCase,
    declaration,
    use,
    splice,
    spliceLength: newline - splice + 1,
  };
});

function assertGenericSplicedIdentifier(result, analysisCase, lifecycle) {
  const declarationStart = Buffer.byteLength(
    genericSource.source.slice(0, analysisCase.declaration),
  );
  const declarationEnd = declarationStart +
    Buffer.byteLength(analysisCase.spelling);
  if (result.partial || result.diagnostics.length !== 0 ||
      result.hover?.name !== analysisCase.name ||
      result.hover.kind !== "object" ||
      result.hover.declaration.sourceName !== genericSource.name ||
      !genericPositionEquals(
        result.hover.declaration.start,
        genericPositionAt(declarationStart),
      ) ||
      !genericPositionEquals(
        result.hover.declaration.end,
        genericPositionAt(declarationEnd),
      )) {
    throw new Error(
      `${lifecycle} spliced identifier hover failed: ${JSON.stringify(result)}`,
    );
  }
}

const nativeSplicedSnapshots = new Map();
for (const analysisCase of genericSplicedIdentifierCases) {
  const cursorDeltas = [
    0,
    Math.floor(analysisCase.splice / 2),
    analysisCase.splice,
    analysisCase.splice + Math.floor(analysisCase.spliceLength / 2),
    analysisCase.splice + analysisCase.spliceLength,
    Buffer.byteLength(analysisCase.spelling),
  ];
  for (const location of [analysisCase.declaration, analysisCase.use]) {
    for (const delta of cursorDeltas) {
      const byteOffset = Buffer.byteLength(
        genericSource.source.slice(0, location),
      ) + delta;
      const wasmResult = compiler.analyzeSource(genericSource, {
        cursor: { sourceName: genericSource.name, byteOffset },
      });
      assertGenericSplicedIdentifier(
        wasmResult, analysisCase, "reused instance",
      );
      const nativeResult = JSON.parse(execFileSync(
        nativeAnalysisPath,
        ["--generic-hover-parity-json", String(byteOffset)],
        { encoding: "utf8" },
      ));
      assert.deepStrictEqual(
        wasmResult,
        nativeResult,
        `native and Wasm spliced identifier snapshots differ at byte ${byteOffset}`,
      );
      nativeSplicedSnapshots.set(byteOffset, nativeResult);
    }
  }
}

for (const analysisCase of genericSplicedIdentifierCases) {
  for (const location of [analysisCase.declaration, analysisCase.use]) {
    const byteOffset = Buffer.byteLength(
      genericSource.source.slice(0, location),
    ) + analysisCase.splice + Math.floor(analysisCase.spliceLength / 2);
    const freshCompiler = await createCompiler(wasmModule);
    try {
      const freshResult = freshCompiler.analyzeSource(genericSource, {
        cursor: { sourceName: genericSource.name, byteOffset },
      });
      assertGenericSplicedIdentifier(
        freshResult, analysisCase, "fresh instance",
      );
      assert.deepStrictEqual(
        freshResult,
        nativeSplicedSnapshots.get(byteOffset),
        `fresh native/Wasm spliced identifier snapshot differs at byte ${byteOffset}`,
      );
    } finally {
      freshCompiler.dispose();
    }
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

const incompleteMacroSource = {
  name: "incomplete-macro.c",
  source: "#define INCOMPLETE_CALL() 1\n" +
    "int main(void) { return INCOMPLETE_CALL(",
};
const incompleteMacroStart = incompleteMacroSource.source.lastIndexOf(
  "INCOMPLETE_CALL(",
);
let incompleteMacroResult = null;
let incompleteMacroError = null;
try {
  incompleteMacroResult = compiler.analyzeSource(incompleteMacroSource, {
    cursor: {
      sourceName: incompleteMacroSource.name,
      byteOffset: incompleteMacroStart +
        Math.floor("INCOMPLETE_CALL".length / 2),
    },
  });
} catch (error) {
  incompleteMacroError = error;
}
if (incompleteMacroResult
  ? !incompleteMacroResult.partial ||
    incompleteMacroResult.diagnostics.length === 0
  : incompleteMacroError?.name !== "AgcLanguageAnalysisError" ||
    !Array.isArray(incompleteMacroError.diagnostics) ||
    incompleteMacroError.diagnostics.length === 0) {
  throw new Error(
    `incomplete macro invocation was marked complete: ${JSON.stringify(incompleteMacroResult || incompleteMacroError)}`,
  );
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
