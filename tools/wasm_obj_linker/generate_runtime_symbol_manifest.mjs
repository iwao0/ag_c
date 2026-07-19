#!/usr/bin/env node

import { readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "../..");
const manifestPath = path.join(scriptDir, "runtime/symbol-manifest.json");
const cOutputPath = path.join(scriptDir, "runtime/generated/runtime-symbols.inc");
const jsOutputPath = path.join(repoRoot, "tools/wasm_js_api/generated/runtime-import-manifest.js");
const docsOutputPath = path.join(scriptDir, "runtime/generated/runtime-symbols.md");
const runtimePartsDir = path.join(scriptDir, "runtime/parts");
const checkOnly = process.argv.includes("--check");

const wasmTypes = new Set(["i32", "i64", "f32", "f64"]);
const bridgeKinds = new Set(["runtime", "synthetic", "host"]);
const availabilityTargets = new Set([
  "wasm32-js",
  "wasm32-object-linker",
  "wasm32-object-runtime",
]);
const availabilityBits = new Map([
  ["wasm32-js", "RUNTIME_AVAILABLE_WASM32_JS"],
  ["wasm32-object-linker", "RUNTIME_AVAILABLE_WASM32_OBJECT_LINKER"],
  ["wasm32-object-runtime", "RUNTIME_AVAILABLE_WASM32_OBJECT_RUNTIME"],
]);

function fail(message) {
  throw new Error(`runtime symbol manifest: ${message}`);
}

function assertObject(value, label) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    fail(`${label} must be an object`);
  }
}

function assertUniqueSortedStrings(values, label, allowed) {
  if (!Array.isArray(values)) fail(`${label} must be an array`);
  const seen = new Set();
  for (const value of values) {
    if (typeof value !== "string" || !value) {
      fail(`${label} contains an invalid name`);
    }
    if (allowed && !allowed.has(value)) fail(`${label} contains unknown ${value}`);
    if (seen.has(value)) fail(`${label} contains duplicate ${value}`);
    seen.add(value);
  }
  const sorted = [...values].sort();
  if (values.some((value, index) => value !== sorted[index])) {
    fail(`${label} must be sorted`);
  }
}

function parseSignature(signature, label) {
  if (signature === "caller") return { kind: "caller", params: [], result: "void" };
  if (typeof signature !== "string") fail(`${label} must be a string`);
  const match = /^((?:i32|i64|f32|f64)(?:,(?:i32|i64|f32|f64))*)?->(void|i32|i64|f32|f64)$/.exec(signature);
  if (!match) fail(`${label} has invalid Wasm signature ${signature}`);
  const params = match[1] ? match[1].split(",") : [];
  const result = match[2];
  for (const type of params) {
    if (!wasmTypes.has(type)) fail(`${label} has invalid parameter type ${type}`);
  }
  if (result !== "void" && !wasmTypes.has(result)) {
    fail(`${label} has invalid result type ${result}`);
  }
  return { kind: "exact", params, result };
}

function validateManifest(manifest) {
  assertObject(manifest, "root");
  if (manifest.version !== 2) fail("version must be 2");
  assertUniqueSortedStrings(manifest.dataSymbols, "dataSymbols");
  if (!Array.isArray(manifest.functions)) fail("functions must be an array");

  const names = new Set();
  let previousName = "";
  for (const entry of manifest.functions) {
    assertObject(entry, "functions entry");
    const label = `function ${entry.cSymbol || "<unknown>"}`;
    if (typeof entry.cSymbol !== "string" || !entry.cSymbol) {
      fail("function has an invalid cSymbol");
    }
    if (names.has(entry.cSymbol)) fail(`functions contains duplicate ${entry.cSymbol}`);
    if (entry.cSymbol < previousName) fail("functions must be sorted by cSymbol");
    names.add(entry.cSymbol);
    previousName = entry.cSymbol;
    if (!bridgeKinds.has(entry.bridge)) fail(`${label} has unknown bridge ${entry.bridge}`);
    if (entry.bridge === "runtime") {
      if (typeof entry.runtimeSymbol !== "string" ||
          !entry.runtimeSymbol.startsWith("__agc_runtime_")) {
        fail(`${label} has an invalid runtimeSymbol`);
      }
    } else if (entry.bridge === "synthetic") {
      if (entry.runtimeSymbol !== null) fail(`${label} synthetic runtimeSymbol must be null`);
      if (entry.signature !== "caller") fail(`${label} synthetic signature must be caller`);
    } else if (typeof entry.runtimeSymbol !== "string" || !entry.runtimeSymbol) {
      fail(`${label} host runtimeSymbol must name its JS implementation`);
    }
    parseSignature(entry.signature, `${label} signature`);
    assertObject(entry.memory, `${label} memory`);
    if (typeof entry.memory.read !== "boolean" ||
        typeof entry.memory.write !== "boolean") {
      fail(`${label} memory.read and memory.write must be booleans`);
    }
    assertUniqueSortedStrings(
      entry.availability, `${label} availability`, availabilityTargets);
    if (typeof entry.importNamespace !== "string" || !entry.importNamespace) {
      fail(`${label} has an invalid importNamespace`);
    }
    const hasJs = entry.availability.includes("wasm32-js");
    if (hasJs) {
      if (entry.importNamespace !== "env") fail(`${label} JS import namespace must be env`);
      if (entry.importGroup !== "math" && entry.importGroup !== "stdio") {
        fail(`${label} has invalid importGroup`);
      }
      if (typeof entry.jsImplementation !== "string" || !entry.jsImplementation) {
        fail(`${label} has an invalid jsImplementation`);
      }
    } else if ("importGroup" in entry || "jsImplementation" in entry) {
      fail(`${label} has JS metadata without wasm32-js availability`);
    }
  }
}

async function runtimeDefinitions() {
  const files = (await readdir(runtimePartsDir))
    .filter((name) => name.endsWith(".c"))
    .sort();
  const definitions = new Set();
  const pattern = /\b(__agc_runtime_[A-Za-z0-9_]+)\s*\([^;{}]*\)\s*\{/g;
  for (const file of files) {
    const source = await readFile(path.join(runtimePartsDir, file), "utf8");
    for (const match of source.matchAll(pattern)) definitions.add(match[1]);
  }
  return definitions;
}

async function runtimePartsSource() {
  const files = (await readdir(runtimePartsDir))
    .filter((name) => name.endsWith(".c"))
    .sort();
  return (await Promise.all(
    files.map((file) => readFile(path.join(runtimePartsDir, file), "utf8")),
  )).join("\n");
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

async function validateDataDefinitions(manifest) {
  const source = await runtimePartsSource();
  const missing = manifest.dataSymbols.filter((name) => {
    const definition = new RegExp(
      `^(?!\\s*(?:extern|static)\\b)[^\\n#();]*\\b${escapeRegExp(name)}\\b\\s*(?:=[^;]*)?;`,
      "m",
    );
    return !definition.test(source);
  });
  if (missing.length) {
    fail(`data symbols have no external runtime definition: ${missing.join(", ")}`);
  }
}

async function validateBridgeTargets(manifest) {
  const definitions = await runtimeDefinitions();
  const missing = manifest.functions
    .filter((entry) => entry.bridge === "runtime" && !definitions.has(entry.runtimeSymbol))
    .map((entry) => `${entry.cSymbol} -> ${entry.runtimeSymbol}`);
  if (missing.length) {
    fail(`bridge targets have no runtime implementation: ${missing.join(", ")}`);
  }
}

async function validateSyntheticHandlers(manifest) {
  const linkerPath = path.join(scriptDir, "ag_wasm_link.c");
  const source = await readFile(linkerPath, "utf8");
  const start = source.indexOf("static int make_printf_stub_body");
  const end = source.indexOf("static void add_runtime_data_symbol", start);
  if (start < 0 || end < 0) fail("cannot locate linker synthetic handlers");
  const handlers = source.slice(start, end);
  const missing = manifest.functions
    .filter((entry) => entry.bridge === "synthetic")
    .filter((entry) => !handlers.includes(`str_eq_lit(name, ${JSON.stringify(entry.cSymbol)})`))
    .map((entry) => entry.cSymbol);
  if (missing.length) {
    fail(`synthetic symbols have no explicit linker handler: ${missing.join(", ")}`);
  }
}

function cString(value) {
  return value === null ? "NULL" : JSON.stringify(value);
}

const wasmTypeBytes = new Map([
  ["i32", "0x7f"],
  ["i64", "0x7e"],
  ["f32", "0x7d"],
  ["f64", "0x7c"],
]);

function cSignature(signature, signatureArrays) {
  const parsed = parseSignature(signature, "generated signature");
  const params = parsed.params.map((type) => wasmTypeBytes.get(type));
  const key = params.join(",");
  let paramArray = "NULL";
  if (params.length > 0) {
    if (!signatureArrays.has(key)) {
      signatureArrays.set(key, `agc_runtime_param_types_${signatureArrays.size}`);
    }
    paramArray = signatureArrays.get(key);
  }
  return {
    kind: parsed.kind === "caller" ? "RUNTIME_SIGNATURE_CALLER" : "RUNTIME_SIGNATURE_EXACT",
    params: paramArray,
    count: parsed.params.length,
    result: parsed.result === "void" ? "0" : wasmTypeBytes.get(parsed.result),
  };
}

function cAvailability(targets) {
  const values = targets.map((target) => availabilityBits.get(target));
  return values.length ? values.join(" | ") : "0";
}

function generateC(manifest) {
  const generatedEntries = manifest.functions
    .filter((item) => item.bridge !== "host");
  const signatureArrays = new Map();
  const signatures = generatedEntries.map((entry) =>
    cSignature(entry.signature, signatureArrays));
  const lines = [
    "/* Generated by tools/wasm_obj_linker/generate_runtime_symbol_manifest.mjs. */",
    "/* Source: tools/wasm_obj_linker/runtime/symbol-manifest.json. */",
    "",
    "static const char *const agc_runtime_data_symbols[] = {",
  ];
  for (const name of manifest.dataSymbols) lines.push(`  ${cString(name)},`);
  lines.push("};", "");
  for (const [key, name] of signatureArrays) {
    lines.push(`static const unsigned char ${name}[] = {${key}};`);
  }
  lines.push("", "static const runtime_symbol_manifest_entry_t agc_runtime_function_symbols[] = {");
  for (let index = 0; index < generatedEntries.length; index++) {
    const entry = generatedEntries[index];
    const signature = signatures[index];
    const kind = entry.bridge === "runtime"
      ? "RUNTIME_SYMBOL_BRIDGE"
      : "RUNTIME_SYMBOL_SYNTHETIC";
    lines.push(
      `  {${cString(entry.cSymbol)}, ${cString(entry.runtimeSymbol)}, ${kind}, ` +
      `${signature.kind}, ${signature.params}, ${signature.count}, ${signature.result}, ` +
      `${entry.memory.read ? 1 : 0}, ${entry.memory.write ? 1 : 0}, ` +
      `${cAvailability(entry.availability)}, ${cString(entry.importNamespace)}},`,
    );
  }
  lines.push("};");
  return `${lines.join("\n")}\n`;
}

function jsFunctionEntry(entry) {
  const signature = parseSignature(entry.signature, `${entry.cSymbol} signature`);
  return {
    cSymbol: entry.cSymbol,
    runtimeSymbol: entry.runtimeSymbol,
    importNamespace: entry.importNamespace,
    importGroup: entry.importGroup,
    implementation: entry.jsImplementation,
    signature,
    memory: entry.memory,
    availability: entry.availability,
    bridge: entry.bridge,
  };
}

function generateJs(manifest) {
  const imports = manifest.functions
    .filter((entry) => entry.availability.includes("wasm32-js"))
    .map(jsFunctionEntry);
  const groups = { env: { math: [], stdio: [] } };
  for (const entry of imports) {
    groups[entry.importNamespace][entry.importGroup].push(entry.cSymbol);
  }
  const payload = JSON.stringify({
    version: manifest.version,
    functions: imports,
    namespaces: groups,
  }, null, 2);
  return `// Generated by tools/wasm_obj_linker/generate_runtime_symbol_manifest.mjs.\n` +
    `// Source: tools/wasm_obj_linker/runtime/symbol-manifest.json.\n\n` +
    `function deepFreeze(value) {\n` +
    `  if (!value || typeof value !== "object" || Object.isFrozen(value)) return value;\n` +
    `  for (const child of Object.values(value)) deepFreeze(child);\n` +
    `  return Object.freeze(value);\n` +
    `}\n\n` +
    `export const AGC_RUNTIME_IMPORT_MANIFEST = deepFreeze(${payload});\n\n` +
    `export function materializeAgcRuntimeImportGroup(namespace, group, implementations) {\n` +
    `  const entries = AGC_RUNTIME_IMPORT_MANIFEST.functions.filter(\n` +
    `    (entry) => entry.importNamespace === namespace && entry.importGroup === group,\n` +
    `  );\n` +
    `  const imports = {};\n` +
    `  for (const entry of entries) {\n` +
    `    const implementation = implementations[entry.implementation];\n` +
    `    if (typeof implementation !== "function") {\n` +
    `      throw new TypeError(\`missing runtime implementation \${entry.implementation} for \${namespace}.\${entry.cSymbol}\`);\n` +
    `    }\n` +
    `    imports[entry.cSymbol] = implementation;\n` +
    `  }\n` +
    `  return imports;\n` +
    `}\n`;
}

function markdownCell(value) {
  return String(value).replace(/\|/g, "\\|");
}

function generateDocs(manifest) {
  const lines = [
    "# Generated Runtime Symbol Catalog",
    "",
    "This file is generated from `tools/wasm_obj_linker/runtime/symbol-manifest.json`.",
    "Do not edit it directly.",
    "",
    "| C symbol | Runtime implementation | Import | Signature | Memory | Availability | Bridge |",
    "|---|---|---|---|---|---|---|",
  ];
  for (const entry of manifest.functions) {
    const memory = entry.memory.read
      ? (entry.memory.write ? "read/write" : "read")
      : (entry.memory.write ? "write" : "none");
    const importName = entry.availability.includes("wasm32-js")
      ? `${entry.importNamespace}.${entry.cSymbol} (${entry.importGroup})`
      : `${entry.importNamespace}.${entry.cSymbol}`;
    lines.push(
      `| \`${markdownCell(entry.cSymbol)}\` | ` +
      `${entry.runtimeSymbol === null ? "-" : `\`${markdownCell(entry.runtimeSymbol)}\``} | ` +
      `\`${markdownCell(importName)}\` | \`${entry.signature}\` | ${memory} | ` +
      `${entry.availability.join(", ")} | ${entry.bridge} |`,
    );
  }
  return `${lines.join("\n")}\n`;
}

async function writeOrCheck(outputPath, expected) {
  if (!checkOnly) {
    await writeFile(outputPath, expected);
    return;
  }
  let actual;
  try {
    actual = await readFile(outputPath, "utf8");
  } catch {
    fail(`generated file is missing: ${path.relative(repoRoot, outputPath)}`);
  }
  if (actual !== expected) {
    fail(`generated file is stale: ${path.relative(repoRoot, outputPath)}`);
  }
}

const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
validateManifest(manifest);
await validateDataDefinitions(manifest);
await validateBridgeTargets(manifest);
await validateSyntheticHandlers(manifest);
await writeOrCheck(cOutputPath, generateC(manifest));
await writeOrCheck(jsOutputPath, generateJs(manifest));
await writeOrCheck(docsOutputPath, generateDocs(manifest));

console.log(`runtime symbol manifest: ${checkOnly ? "ok" : "generated"}`);
