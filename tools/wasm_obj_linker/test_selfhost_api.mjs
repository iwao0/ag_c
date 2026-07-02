import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

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
const { instance } = await WebAssembly.instantiate(source, {});
const { memory, malloc, free, agc_wasm_link_objects } = instance.exports;
if (!memory || !malloc || !free || !agc_wasm_link_objects) {
  throw new Error("missing linker API exports");
}

let u8 = new Uint8Array(memory.buffer);
let view = new DataView(memory.buffer);
function refresh() {
  u8 = new Uint8Array(memory.buffer);
  view = new DataView(memory.buffer);
}
function allocBytes(bytes) {
  const p = Number(malloc(BigInt(bytes.length || 1)));
  refresh();
  u8.set(bytes, p);
  return p;
}
function allocCString(s) {
  return allocBytes(Buffer.from(`${s}\0`, "utf8"));
}
function setU64(addr, value) {
  view.setBigUint64(addr, BigInt(value), true);
}
function getU64(addr) {
  return Number(view.getBigUint64(addr, true));
}

const objBytes = fs.readFileSync(objPath);
const objPtr = allocBytes(objBytes);
const descPtr = Number(malloc(16n));
refresh();
setU64(descPtr, objPtr);
setU64(descPtr + 8, objBytes.length);

const exportMainPtr = allocCString("main");
const exportsPtr = Number(malloc(8n));
refresh();
setU64(exportsPtr, exportMainPtr);

const outLenPtr = Number(malloc(8n));
refresh();
setU64(outLenPtr, 0);

const linkedPtrBig = agc_wasm_link_objects(BigInt(descPtr), 1n, BigInt(exportsPtr), 1n, 1n, BigInt(outLenPtr));
refresh();
const linkedPtr = Number(linkedPtrBig);
const linkedLen = getU64(outLenPtr);
if (!linkedPtr || linkedLen < 8) throw new Error("linker API returned no wasm");

const linked = Buffer.from(u8.slice(linkedPtr, linkedPtr + linkedLen));
if (linked[0] !== 0x00 || linked[1] !== 0x61 || linked[2] !== 0x73 || linked[3] !== 0x6d) {
  throw new Error("linker API output is not a wasm module");
}
fs.writeFileSync(linkedPath, linked);

free(objPtr);
free(descPtr);
free(exportMainPtr);
free(exportsPtr);
free(outLenPtr);
free(linkedPtr);

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
