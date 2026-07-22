import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { AgcLinkError, createLinker } from "./ag-wasm-link.js";

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), "../..");
const wasmPath = process.argv[2] || path.join(root, "build/wasm_linker_selfhost/ag_wasm_link.wasm");
const outDir = path.join(root, "build/wasm_linker_selfhost_api_smoke");
fs.mkdirSync(outDir, { recursive: true });

const linkedPath = path.join(outDir, "linked_from_api.wasm");

const source = fs.readFileSync(wasmPath);
const linker = await createLinker(source);
const stderrChunks = [];
const terminations = [];
const diagnosticLinker = await createLinker(source, {
  onStderr(chunk) {
    stderrChunks.push(chunk);
  },
  onTerminate(event) {
    terminations.push(event);
  },
});

function compileObject(name, sourceText) {
  const srcPath = path.join(outDir, `${name}.c`);
  const objPath = path.join(outDir, `${name}.o`);
  fs.writeFileSync(srcPath, sourceText);
  execFileSync(path.join(root, "build/ag_c_wasm"), ["-c", "-o", objPath, srcPath], { stdio: "inherit" });
  return fs.readFileSync(objPath);
}

function assertWasm(bytes) {
  if (bytes[0] !== 0x00 || bytes[1] !== 0x61 || bytes[2] !== 0x73 || bytes[3] !== 0x6d) {
    throw new Error("linker API output is not a wasm module");
  }
}

function readUleb(bytes, state) {
  let value = 0;
  for (let shift = 0; shift < 35 && state.pos < bytes.length; shift += 7) {
    const byte = bytes[state.pos++];
    value |= (byte & 0x7f) << shift;
    if ((byte & 0x80) === 0) return value >>> 0;
  }
  throw new Error("invalid wasm uleb");
}

function inspectLayout(bytes) {
  const layout = { memory: null, table: null, hasStart: false };
  const state = { pos: 8 };
  while (state.pos < bytes.length) {
    const id = bytes[state.pos++];
    const size = readUleb(bytes, state);
    const end = state.pos + size;
    if (id === 8) layout.hasStart = true;
    if (id === 4 || id === 5) {
      if (readUleb(bytes, state) !== 1) throw new Error("unexpected limits vector");
      if (id === 4 && bytes[state.pos++] !== 0x70) throw new Error("unexpected table type");
      const flags = readUleb(bytes, state);
      const initial = readUleb(bytes, state);
      const maximum = (flags & 1) !== 0 ? readUleb(bytes, state) : null;
      if (id === 4) layout.table = { flags, initial, maximum };
      else layout.memory = { flags, initial, maximum };
    }
    state.pos = end;
  }
  return layout;
}

const singleObj = compileObject("single", "int main(void) { return 42; }\n");
const linked = Buffer.from(linker.link([singleObj], { exports: ["main"] }));
assertWasm(linked);
fs.writeFileSync(linkedPath, linked);

for (const [exportSpec, signed] of [
  ["missing_entry", false],
  [{ name: "missing_signed_entry", signature: "v()" }, true],
]) {
  try {
    linker.link([singleObj], { exports: [exportSpec], useStdlib: false });
    throw new Error("missing export unexpectedly linked");
  } catch (error) {
    const exportName = typeof exportSpec === "string" ? exportSpec : exportSpec.name;
    if (!(error instanceof AgcLinkError) ||
        error.code !== "AGC_LINK_MISSING_EXPORT" ||
        error.details.exportName !== exportName ||
        error.details.signed !== signed) {
      throw error;
    }
  }
}

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

const mainObj = compileObject("main_xtu", "int other(void); int main(void) { return other() + 1; }\n");
const otherObj = compileObject("other_xtu", "int other(void) { return 41; }\n");
const linkedXtuPath = path.join(outDir, "linked_xtu_from_api.wasm");
const linkedXtu = Buffer.from(linker.link([mainObj, otherObj], {
  exports: ["main"],
  useStdlib: false,
}));
assertWasm(linkedXtu);
fs.writeFileSync(linkedXtuPath, linkedXtu);

try {
  execFileSync("wasm-validate", [linkedXtuPath], { stdio: "inherit" });
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

try {
  const out = execFileSync("wasm-interp", [linkedXtuPath, "--run-all-exports"], { encoding: "utf8" });
  if (!out.includes("main() => i32:42")) throw new Error(out.trim() || "wasm-interp produced no main result");
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

console.log(`ag_wasm_link selfhost API xtu smoke: ok (${linkedXtuPath})`);

const layoutObj = compileObject(
  "layout_options",
  "int add1(int x){return x+1;} int (*fp)(int)=add1; int main(void){return fp(41);}\n",
);
const linkedLayout = Buffer.from(linker.link([layoutObj], {
  exports: ["main"],
  useStdlib: false,
  initialMemoryPages: 1,
  maximumMemoryPages: 4,
  stackSize: 131072,
  maximumTableElements: 8,
}));
const layout = inspectLayout(linkedLayout);
if (!layout.memory || layout.memory.flags !== 1 || layout.memory.initial !== 3 ||
    layout.memory.maximum !== 4 || !layout.table || layout.table.flags !== 1 ||
    layout.table.initial !== 3 || layout.table.maximum !== 8 || layout.hasStart) {
  throw new Error(`linker API layout options were not encoded: ${JSON.stringify(layout)}`);
}
console.log("ag_wasm_link selfhost API layout options smoke: ok");

try {
  stderrChunks.length = 0;
  terminations.length = 0;
  diagnosticLinker.link([new Uint8Array([1, 2, 3, 4])], { exports: ["main"] });
  throw new Error("invalid object unexpectedly linked");
} catch (err) {
  const message = String(err && err.message ? err.message : err);
  if (!message.includes("invalid linker API object slice")) {
    throw new Error(`invalid object did not surface linker diagnostics: ${message}`);
  }
  if (!stderrChunks.join("").includes("invalid linker API object slice")) {
    throw new Error("invalid object did not stream diagnostics through onStderr");
  }
  if (terminations.length !== 1 || terminations[0].kind !== "exit" || terminations[0].status !== 1) {
    throw new Error(`invalid object did not report exit(1): ${JSON.stringify(terminations)}`);
  }
}

console.log("ag_wasm_link selfhost API diagnostics smoke: ok");
