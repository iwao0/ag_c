#!/usr/bin/env node

import { readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "../..");
const manifestPath = path.join(scriptDir, "runtime/symbol-manifest.json");
const cOutputPath = path.join(scriptDir, "runtime/generated/runtime-symbols.inc");
const jsOutputPath = path.join(repoRoot, "tools/wasm_js_api/generated/runtime-import-manifest.js");
const runtimePartsDir = path.join(scriptDir, "runtime/parts");
const checkOnly = process.argv.includes("--check");

function fail(message) {
  throw new Error(`runtime symbol manifest: ${message}`);
}

function assertObject(value, label) {
  if (!value || typeof value !== "object" || Array.isArray(value)) fail(`${label} must be an object`);
}

function assertUniqueSortedStrings(values, label) {
  if (!Array.isArray(values)) fail(`${label} must be an array`);
  const seen = new Set();
  for (const value of values) {
    if (typeof value !== "string" || !value) fail(`${label} contains an invalid name`);
    if (seen.has(value)) fail(`${label} contains duplicate ${value}`);
    seen.add(value);
  }
  const sorted = [...values].sort();
  if (values.some((value, index) => value !== sorted[index])) fail(`${label} must be sorted`);
}

function validateManifest(manifest) {
  assertObject(manifest, "root");
  if (manifest.version !== 1) fail("version must be 1");
  assertObject(manifest.linker, "linker");
  assertUniqueSortedStrings(manifest.linker.data, "linker.data");
  if (!Array.isArray(manifest.linker.functions)) fail("linker.functions must be an array");

  const names = new Set();
  let previousName = "";
  for (const entry of manifest.linker.functions) {
    assertObject(entry, "linker.functions entry");
    if (typeof entry.name !== "string" || !entry.name) fail("linker function has an invalid name");
    if (names.has(entry.name)) fail(`linker.functions contains duplicate ${entry.name}`);
    if (entry.name < previousName) fail("linker.functions must be sorted by name");
    names.add(entry.name);
    previousName = entry.name;
    if (entry.kind === "bridge") {
      if (typeof entry.target !== "string" || !entry.target.startsWith("__agc_runtime_")) {
        fail(`bridge ${entry.name} has an invalid target`);
      }
    } else if (entry.kind === "synthetic") {
      if ("target" in entry) fail(`synthetic symbol ${entry.name} must not have a target`);
    } else {
      fail(`${entry.name} has unknown kind ${entry.kind}`);
    }
  }

  assertObject(manifest.jsImports, "jsImports");
  assertObject(manifest.jsImports.env, "jsImports.env");
  const groups = Object.keys(manifest.jsImports.env).sort();
  if (groups.join(",") !== "math,stdio") fail("jsImports.env must contain exactly math and stdio");
  const allImports = new Set();
  for (const group of groups) {
    const values = manifest.jsImports.env[group];
    assertUniqueSortedStrings(values, `jsImports.env.${group}`);
    for (const value of values) {
      if (allImports.has(value)) fail(`JS import ${value} belongs to more than one group`);
      allImports.add(value);
    }
  }
}

async function runtimeDefinitions() {
  const files = (await readdir(runtimePartsDir)).filter((name) => name.endsWith(".c")).sort();
  const definitions = new Set();
  const definitionPattern = /\b(__agc_runtime_[A-Za-z0-9_]+)\s*\([^;{}]*\)\s*\{/g;
  for (const file of files) {
    const source = await readFile(path.join(runtimePartsDir, file), "utf8");
    for (const match of source.matchAll(definitionPattern)) definitions.add(match[1]);
  }
  return definitions;
}

async function runtimePartsSource() {
  const files = (await readdir(runtimePartsDir)).filter((name) => name.endsWith(".c")).sort();
  return (await Promise.all(files.map((file) => readFile(path.join(runtimePartsDir, file), "utf8"))))
    .join("\n");
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

async function validateDataDefinitions(manifest) {
  const source = await runtimePartsSource();
  const missing = manifest.linker.data.filter((name) => {
    const definition = new RegExp(
      `^(?!\\s*(?:extern|static)\\b)[^\\n#();]*\\b${escapeRegExp(name)}\\b\\s*(?:=[^;]*)?;`,
      "m",
    );
    return !definition.test(source);
  });
  if (missing.length) fail(`data symbols have no external runtime definition: ${missing.join(", ")}`);
}

async function validateBridgeTargets(manifest) {
  const definitions = await runtimeDefinitions();
  const missing = manifest.linker.functions
    .filter((entry) => entry.kind === "bridge" && !definitions.has(entry.target))
    .map((entry) => `${entry.name} -> ${entry.target}`);
  if (missing.length) fail(`bridge targets have no runtime implementation: ${missing.join(", ")}`);
}

async function validateSyntheticHandlers(manifest) {
  const linkerPath = path.join(scriptDir, "ag_wasm_link.c");
  const source = await readFile(linkerPath, "utf8");
  const start = source.indexOf("static int make_printf_stub_body");
  const end = source.indexOf("static void add_runtime_data_symbol", start);
  if (start < 0 || end < 0) fail("cannot locate linker synthetic handlers");
  const handlers = source.slice(start, end);
  const missing = manifest.linker.functions
    .filter((entry) => entry.kind === "synthetic")
    .filter((entry) => !handlers.includes(`str_eq_lit(name, ${JSON.stringify(entry.name)})`))
    .map((entry) => entry.name);
  if (missing.length) fail(`synthetic symbols have no explicit linker handler: ${missing.join(", ")}`);
}

function cString(value) {
  return JSON.stringify(value);
}

function generateC(manifest) {
  const lines = [
    "/* Generated by tools/wasm_obj_linker/generate_runtime_symbol_manifest.mjs. */",
    "/* Source: tools/wasm_obj_linker/runtime/symbol-manifest.json. */",
    "",
    "static const char *const agc_runtime_data_symbols[] = {",
  ];
  for (const name of manifest.linker.data) lines.push(`  ${cString(name)},`);
  lines.push("};", "", "static const runtime_symbol_manifest_entry_t agc_runtime_function_symbols[] = {");
  for (const entry of manifest.linker.functions) {
    const target = entry.kind === "bridge" ? cString(entry.target) : "NULL";
    const kind = entry.kind === "bridge" ? "RUNTIME_SYMBOL_BRIDGE" : "RUNTIME_SYMBOL_SYNTHETIC";
    lines.push(`  {${cString(entry.name)}, ${target}, ${kind}},`);
  }
  lines.push("};");
  return `${lines.join("\n")}\n`;
}

function generateJs(manifest) {
  const math = JSON.stringify(manifest.jsImports.env.math, null, 2).replace(/^/gm, "      ");
  const stdio = JSON.stringify(manifest.jsImports.env.stdio, null, 2).replace(/^/gm, "      ");
  return `// Generated by tools/wasm_obj_linker/generate_runtime_symbol_manifest.mjs.\n` +
    `// Source: tools/wasm_obj_linker/runtime/symbol-manifest.json.\n\n` +
    `export const AGC_RUNTIME_IMPORT_MANIFEST = Object.freeze({\n` +
    `  version: ${manifest.version},\n` +
    `  namespaces: Object.freeze({\n` +
    `    env: Object.freeze({\n` +
    `      math: Object.freeze(${math.trimStart()}),\n` +
    `      stdio: Object.freeze(${stdio.trimStart()}),\n` +
    `    }),\n` +
    `  }),\n` +
    `});\n`;
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

console.log(`runtime symbol manifest: ${checkOnly ? "ok" : "generated"}`);
