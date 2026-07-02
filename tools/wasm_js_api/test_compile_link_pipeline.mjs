import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { createToolchain } from "./agc-toolchain.js";

const compilerWasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const linkerWasmPath = process.argv[3] || "build/wasm_linker_selfhost/ag_wasm_link.wasm";
const outDir = "build/wasm_js_pipeline_smoke";
await mkdir(outDir, { recursive: true });

const toolchain = await createToolchain({
  compilerWasm: await readFile(compilerWasmPath),
  linkerWasm: await readFile(linkerWasmPath),
});

const mainSource = "int other(void); int main(void) { return other() + 1; }\n";
const otherSource = "int other(void) { return 41; }\n";
const mainObj = toolchain.compileObject(mainSource);
const otherObj = toolchain.compileObject(otherSource);

const mainObjPath = path.join(outDir, "main_from_compiler_api.o");
const otherObjPath = path.join(outDir, "other_from_compiler_api.o");
await writeFile(mainObjPath, mainObj);
await writeFile(otherObjPath, otherObj);

try {
  const dump = execFileSync("wasm-objdump", ["-x", mainObjPath], { encoding: "utf8" });
  if (!dump.includes("linking") || !dump.includes("reloc.CODE")) {
    throw new Error("compiler API object is missing linking metadata or call relocation");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

const linked = toolchain.compileLinkedWasm([mainSource, otherSource], {
  exports: ["main"],
  useStdlib: false,
});
if (linked[0] !== 0x00 || linked[1] !== 0x61 || linked[2] !== 0x73 || linked[3] !== 0x6d) {
  throw new Error("pipeline output is not a wasm module");
}

const linkedPath = path.join(outDir, "linked_from_wasm_compiler_and_linker.wasm");
await writeFile(linkedPath, linked);

try {
  execFileSync("wasm-validate", [linkedPath], { stdio: "inherit" });
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

try {
  const out = execFileSync("wasm-interp", [linkedPath, "--run-all-exports"], { encoding: "utf8" });
  if (!out.includes("main() => i32:42")) {
    throw new Error(out.trim() || "wasm-interp produced no main result");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

const instantiated = await toolchain.instantiateLinkedWasm([mainSource, otherSource], {
  exports: ["main"],
  useStdlib: false,
});
if (instantiated.instance.exports.main() !== 42) {
  throw new Error("instantiated pipeline main() did not return 42");
}

const mathSource = `
double sin(double);
double sqrt(double);
double pow(double, double);
int main(void) {
  return (int)(sin(1.5707963267948966) * 1000.0) + (int)sqrt(4.0) + (int)pow(2.0, 3.0);
}
`;
const mathLinked = toolchain.compileLinkedWasm(mathSource, { exports: ["main"], useStdlib: false });
const mathLinkedPath = path.join(outDir, "linked_math_imports_from_api.wasm");
await writeFile(mathLinkedPath, mathLinked);
try {
  const dump = execFileSync("wasm-objdump", ["-x", mathLinkedPath], { encoding: "utf8" });
  if (!dump.includes("env.sin") ||
      !dump.includes("env.sqrt") ||
      !dump.includes("env.pow")) {
    throw new Error("linked math wasm did not import JS math helpers");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}
const mathInstantiated = await toolchain.instantiateLinkedWasm(mathSource, {
  exports: ["main"],
  useStdlib: false,
});
if (mathInstantiated.instance.exports.main() !== 1010) {
  throw new Error("instantiated math pipeline did not use JS math imports");
}

console.log(`ag_c wasm JS compile+link pipeline smoke: ok (${linkedPath})`);
