import { readFile } from "node:fs/promises";

const backendFiles = [
  "src/arch/arm64_apple.c",
  "src/arch/arm64_apple_ir.c",
  "src/arch/wasm32_ir.c",
  "src/arch/wasm32_obj.c",
];

const actual = new Map();
const bridgeRe = /\b(?:psx?_ctx_[A-Za-z0-9_]+|ir_abi_classify_function_param)\b/g;
for (const file of backendFiles) {
  const source = await readFile(file, "utf8");
  if (/#[ \t]*include[^\n]*parser\/semantic_ctx\.h/.test(source)) {
    throw new Error(`${file} directly includes parser/semantic_ctx.h`);
  }
  for (const match of source.matchAll(bridgeRe)) {
    const key = `${file}:${match[0]}`;
    actual.set(key, (actual.get(key) || 0) + 1);
  }
}

if (actual.size) {
  const violations = [...actual.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([key, count]) => `${key}: ${count}`);
  throw new Error(
    "backend must not read parser context or invoke parser-backed ABI " +
    `classification:\n${violations.join("\n")}`,
  );
}

console.log("design invariants: ok (backend parser context isolation verified)");
