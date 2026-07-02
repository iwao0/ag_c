import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { createCompiler } from "./agc-wasm.js";

const wasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const wasm = await readFile(wasmPath);
const stderrChunks = [];
const terminations = [];
const compiler = await createCompiler(wasm, {
  onStderr(chunk) {
    stderrChunks.push(chunk);
  },
  onTerminate(event) {
    terminations.push(event);
  },
});
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

try {
  stderrChunks.length = 0;
  terminations.length = 0;
  compiler.compileWat("int main( {\n");
  throw new Error("invalid source unexpectedly compiled");
} catch (err) {
  const message = String(err && err.message ? err.message : err);
  if (!message.includes("E") || !message.includes("実際のトークン") || message.includes("timed out")) {
    throw new Error(`invalid source did not surface compiler diagnostics: ${message}`);
  }
  if (!stderrChunks.join("").includes("実際のトークン")) {
    throw new Error("invalid source did not stream diagnostics through onStderr");
  }
  if (terminations.length !== 1 || terminations[0].kind !== "exit" || terminations[0].status !== 1) {
    throw new Error(`invalid source did not report exit(1): ${JSON.stringify(terminations)}`);
  }
}

const objectBytes = compiler.compileObject("int other(void); int main(void){return other();}\n");
if (objectBytes[0] !== 0x00 || objectBytes[1] !== 0x61 ||
    objectBytes[2] !== 0x73 || objectBytes[3] !== 0x6d) {
  throw new Error("compileObject output is not a wasm object module");
}
const outDir = "build/wasm_selfhost_api_smoke";
await mkdir(outDir, { recursive: true });
const objPath = path.join(outDir, "compile_object_from_api.o");
await writeFile(objPath, objectBytes);
try {
  const out = execFileSync("wasm-objdump", ["-x", objPath], { encoding: "utf8" });
  if (!out.includes("linking") || !out.includes("reloc.CODE")) {
    throw new Error("compileObject output is missing Wasm object metadata");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

console.log("ag_c wasm JS API smoke: ok");
