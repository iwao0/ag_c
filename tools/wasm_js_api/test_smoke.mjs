import { readFile } from "node:fs/promises";
import { createCompiler } from "./agc-wasm.js";

const wasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const wasm = await readFile(wasmPath);
const compiler = await createCompiler(wasm);
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

const largeSource = `/*${"x".repeat(40000)}*/\nint main(){return 7;}\n`;
const largeWat = compiler.compileWat(largeSource);
if (!largeWat.includes("(return (i32.const 7))")) {
  throw new Error("heap-buffer compile did not handle source larger than fixed buffer");
}

console.log("ag_c wasm JS API smoke: ok");
