import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { createLinker } from "./ag-wasm-link.js";

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), "../..");
const wasmPath = process.argv[2] || path.join(root, "build/wasm_linker_selfhost/ag_wasm_link.wasm");
const outDir = path.join(root, "build/wasm_linker_selfhost_api_smoke");
fs.mkdirSync(outDir, { recursive: true });

const srcPath = path.join(outDir, "simple.c");
const objPath = path.join(outDir, "simple.o");
const linkedPath = path.join(outDir, "linked_from_api.wasm");
fs.writeFileSync(srcPath, "int main(void) { return 42; }\n");
execFileSync(path.join(root, "build/ag_c_wasm"), ["-c", "-o", objPath, srcPath], { stdio: "inherit" });

const source = fs.readFileSync(wasmPath);
const objBytes = fs.readFileSync(objPath);
const linker = await createLinker(source);
const linked = Buffer.from(linker.link([objBytes], { exports: ["main"] }));
if (linked[0] !== 0x00 || linked[1] !== 0x61 || linked[2] !== 0x73 || linked[3] !== 0x6d) {
  throw new Error("linker API output is not a wasm module");
}
fs.writeFileSync(linkedPath, linked);

try {
  execFileSync("wasm-validate", [linkedPath], { stdio: "inherit" });
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

try {
  const out = execFileSync("wasm-interp", [linkedPath, "--run-all-exports"], { encoding: "utf8" });
  if (!out.includes("main() => i32:42")) throw new Error(out.trim() || "wasm-interp produced no main result");
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

console.log(`ag_wasm_link selfhost API smoke: ok (${linkedPath})`);
