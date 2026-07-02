import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { createToolchain } from "./agc-toolchain.js";
import { inlineStandardIncludes } from "./agc-include-inline.js";

const compilerWasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const linkerWasmPath = process.argv[3] || "build/wasm_linker_selfhost/ag_wasm_link.wasm";
const outDir = process.env.WASM_JS_E2E_PIPELINE_DIR || "build/wasm_js_e2e_pipeline";
const interpTimeoutMs = Number(process.env.WASM_JS_E2E_PIPELINE_TIMEOUT_MS || "5000");

let listFail = false;
let verbose = false;
let start = 0;
let limit = 0;
let progressEvery = Number(process.env.WASM_JS_E2E_PIPELINE_PROGRESS_EVERY || "25");
for (const arg of process.argv.slice(4)) {
  if (arg === "--list-fail") {
    listFail = true;
  } else if (arg === "--verbose") {
    verbose = true;
  } else if (arg.startsWith("--start=")) {
    start = Number(arg.slice("--start=".length));
    if (!Number.isInteger(start) || start < 0) {
      throw new Error(`invalid --start value: ${arg}`);
    }
  } else if (arg.startsWith("--limit=")) {
    limit = Number(arg.slice("--limit=".length));
    if (!Number.isInteger(limit) || limit < 0) {
      throw new Error(`invalid --limit value: ${arg}`);
    }
  } else if (arg.startsWith("--progress-every=")) {
    progressEvery = Number(arg.slice("--progress-every=".length));
    if (!Number.isInteger(progressEvery) || progressEvery < 0) {
      throw new Error(`invalid --progress-every value: ${arg}`);
    }
  } else {
    throw new Error(`unknown option: ${arg}`);
  }
}

await mkdir(outDir, { recursive: true });

function fixtureSafeName(src) {
  return src.replace(/^test\/fixtures\//, "").replace(/\//g, "__").replace(/\.c$/, "");
}

function skipReason(src) {
  if (src === "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c") {
    return "multi-TU link fixture component without main";
  }
  if (src.includes("/should_reject/")) {
    return "negative fixture";
  }
  return null;
}

function parseTestCases(text) {
  const cases = [];
  const singleRe = /\{\s*"([^"]+)",\s*"([^"]+)",\s*(CASE_[A-Z_]+),\s*"([^"]+\.c)",\s*(-?\d+),\s*([-+0-9.eE]+)\s*\}/g;
  for (const m of text.matchAll(singleRe)) {
    const kind = m[3];
    const src = m[4];
    if (!kind.endsWith("_FILE")) continue;
    const reason = skipReason(src);
    if (reason) {
      cases.push({ kind: "skip", name: m[2], sources: [src], reason });
      continue;
    }
    cases.push({
      kind,
      name: m[2],
      sources: [src],
      expected: kind === "CASE_ASSERT_FILE" ? 0 : Number(m[5]),
    });
  }

  const link2Re = /\{\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+\.c)",\s*"([^"]+\.c)",\s*(-?\d+)\s*\}/g;
  for (const m of text.matchAll(link2Re)) {
    cases.push({
      kind: "LINK2",
      name: m[2],
      sources: [m[3], m[4]],
      expected: Number(m[5]),
    });
  }

  const seen = new Set();
  return cases.filter((tc) => {
    const key = `${tc.kind}:${tc.sources.join("+")}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

const loadInclude = async (name) => readFile(new URL(`../../include/${name}`, import.meta.url), "utf8");

async function loadFixtureSource(src) {
  const text = await readFile(src, "utf8");
  return inlineStandardIncludes(text, { loadInclude });
}

function commandAvailable(command) {
  try {
    execFileSync(command, ["--version"], { stdio: "ignore" });
    return true;
  } catch {
    return false;
  }
}

function firstLine(text) {
  return String(text || "").split(/\r?\n/, 1)[0];
}

const compilerModule = await WebAssembly.compile(await readFile(compilerWasmPath));
const linkerModule = await WebAssembly.compile(await readFile(linkerWasmPath));
const runtimeObject = await readFile("build/libagc_runtime.o");

const testE2eText = await readFile("test/test_e2e.c", "utf8");
const allCases = parseTestCases(testE2eText);
const cases = limit > 0 ? allCases.slice(start, start + limit) : allCases.slice(start);
const failures = [];
let scanned = 0;
let skipped = 0;
let linked = 0;
let validated = 0;
let ran = 0;
let skippedRunTools = 0;

const canValidate = commandAvailable("wasm-validate");
const canRun = commandAvailable("wasm-interp");

for (const tc of cases) {
  if (tc.kind === "skip") {
    skipped++;
    if (verbose) console.log(`SKIP ${tc.sources[0]}\t${tc.reason}`);
    continue;
  }

  scanned++;
  if (progressEvery > 0 && (scanned === 1 || scanned % progressEvery === 0)) {
    console.error(`[wasm-js-e2e] ${scanned}/${cases.length}: ${tc.sources.join(" + ")}`);
  }
  const label = tc.sources.map(fixtureSafeName).join("__plus__");
  const wasmPath = path.join(outDir, `${label}.wasm`);

  try {
    const sources = await Promise.all(tc.sources.map(loadFixtureSource));
    const toolchain = await createToolchain({
      compilerWasm: compilerModule,
      linkerWasm: linkerModule,
      runtimeObject,
    });
    const wasm = toolchain.compileLinkedWasm(sources, {
      exports: ["main"],
      useStdlib: true,
    });
    await writeFile(wasmPath, wasm);
    linked++;

    if (canValidate) {
      execFileSync("wasm-validate", [wasmPath], { stdio: "pipe" });
      validated++;
    }

    if (!canRun) {
      skippedRunTools++;
      if (verbose) console.log(`PASS ${tc.sources[0]}\tlink-only`);
      continue;
    }

    const out = execFileSync("wasm-interp", [wasmPath, "--run-all-exports"], {
      encoding: "utf8",
      timeout: interpTimeoutMs,
      stdio: ["ignore", "pipe", "pipe"],
    });
    const expectedLine = `main() => i32:${tc.expected}`;
    if (!out.includes(expectedLine)) {
      throw new Error(`result mismatch; expected ${expectedLine}; got ${out.trim() || "(no output)"}`);
    }
    ran++;
    if (verbose) console.log(`PASS ${tc.sources[0]}\trun`);
  } catch (err) {
    const stderr = err && err.stderr ? err.stderr.toString() : "";
    const message = firstLine(stderr) || firstLine(err && err.message ? err.message : String(err));
    failures.push(`${tc.sources.join(" + ")}\t${message}`);
    if (verbose) console.log(`FAIL ${tc.sources[0]}\t${message}`);
  }
}

const failuresPath = path.join(outDir, "failures.txt");
await writeFile(failuresPath, failures.join("\n") + (failures.length ? "\n" : ""));

console.log("==== wasm JS e2e pipeline ====");
console.log(`Total registered: ${allCases.length}`);
console.log(`Scanned:          ${scanned}`);
console.log(`Pass:             ${scanned - failures.length}`);
console.log(`Fail:             ${failures.length}`);
console.log(`Skip:             ${skipped}`);
console.log(`Linked:           ${linked}`);
console.log(`Validate:         ${canValidate ? 1 : 0}`);
console.log(`Validated:        ${validated}`);
console.log(`Run tool:         ${canRun ? 1 : 0}`);
console.log(`Ran:              ${ran}`);
console.log(`Skip run tools:   ${skippedRunTools}`);
console.log(`Log:              ${failuresPath}`);

if (failures.length) {
  const shown = listFail ? failures : failures.slice(0, 20);
  for (const failure of shown) console.log(failure);
  process.exit(1);
}
