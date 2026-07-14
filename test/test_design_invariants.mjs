import { readFile, readdir } from "node:fs/promises";

async function sourceFilesUnder(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = `${directory}/${entry.name}`;
    if (entry.isDirectory()) {
      files.push(...(await sourceFilesUnder(path)));
    } else if (entry.isFile() && /\.[ch]$/.test(entry.name)) {
      files.push(path);
    }
  }
  return files;
}

const irFiles = (await readdir("src/ir", { withFileTypes: true }))
  .filter((entry) => entry.isFile() && entry.name.endsWith(".c"))
  .map((entry) => `src/ir/${entry.name}`)
  .sort();

const backendFiles = [
  "src/arch/arm64_apple.c",
  "src/arch/arm64_apple_ir.c",
  "src/arch/wasm32_ir.c",
  "src/arch/wasm32_obj.c",
  ...irFiles,
];

const actual = new Map();
const contextBridgeRe =
  /\b(?:psx?_ctx_[A-Za-z0-9_]+|ir_abi_classify_function_param)\b/g;
const irSymbolTypeRe = /\bps_(?:lvar|gvar)_get_decl_type\b/g;
for (const file of backendFiles) {
  const source = await readFile(file, "utf8");
  if (/#[ \t]*include[^\n]*parser\/semantic_ctx\.h/.test(source)) {
    throw new Error(`${file} directly includes parser/semantic_ctx.h`);
  }
  const forbidden = file.startsWith("src/ir/")
    ? [contextBridgeRe, irSymbolTypeRe]
    : [contextBridgeRe];
  for (const pattern of forbidden) {
    for (const match of source.matchAll(pattern)) {
      const key = `${file}:${match[0]}`;
      actual.set(key, (actual.get(key) || 0) + 1);
    }
  }
}

if (actual.size) {
  const violations = [...actual.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([key, count]) => `${key}: ${count}`);
  throw new Error(
    "backend must not read parser context; IR must not recover types from symbols or invoke parser-backed ABI " +
    `classification:\n${violations.join("\n")}`,
  );
}

const sourceFiles = (await sourceFilesUnder("src")).sort();
const legacyTypeMutationRe =
  /\b(?:psx_ctx_add_tag_member|ps_ctx_typedef_set_decl_type|ps_tag_member_set_decl_type|ps_tag_member_decl_type_mut|tag_member_record_set_decl_type|typedef_record_set_decl_type)\b/g;
const legacyTypeMutations = [];
for (const file of sourceFiles) {
  const source = await readFile(file, "utf8");
  for (const match of source.matchAll(legacyTypeMutationRe)) {
    legacyTypeMutations.push(`${file}:${match[0]}`);
  }
}
if (legacyTypeMutations.length) {
  throw new Error(
    "canonical typedef and tag-member records must not expose generic type mutation APIs:\n" +
      legacyTypeMutations.sort().join("\n"),
  );
}

console.log(
  "design invariants: ok (backend parser context isolation and canonical type ownership verified)",
);
