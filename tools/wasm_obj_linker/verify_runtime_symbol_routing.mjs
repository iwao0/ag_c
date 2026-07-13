#!/usr/bin/env node

import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const manifestPath = path.join(scriptDir, "runtime/symbol-manifest.json");
const [inputDumpPath, outputDumpPath] = process.argv.slice(2);

if (!inputDumpPath || !outputDumpPath) {
  throw new Error("usage: verify_runtime_symbol_routing.mjs <input.objdump> <output.objdump>");
}

function envFunctionImports(dump) {
  return new Set(
    [...dump.matchAll(/^ - func\[\d+\].* <- env\.([^\s]+)$/gm)].map((match) => match[1]),
  );
}

function difference(left, right) {
  return [...left].filter((name) => !right.has(name)).sort();
}

const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
const declared = new Set(
  manifest.linker.functions.filter((entry) => entry.kind === "bridge").map((entry) => entry.name),
);
const inputImports = envFunctionImports(await readFile(inputDumpPath, "utf8"));
const outputImports = envFunctionImports(await readFile(outputDumpPath, "utf8"));

if (!inputImports.size) throw new Error("runtime routing fixture has no env function imports");

const undeclaredInput = difference(inputImports, declared);
const missingOutput = difference(inputImports, outputImports);
const unexpectedOutput = difference(outputImports, inputImports);
if (undeclaredInput.length || missingOutput.length || unexpectedOutput.length) {
  throw new Error(
    "runtime symbol routing mismatch; " +
    `fixture imports absent from manifest: ${undeclaredInput.join(", ") || "none"}; ` +
    `imports lost by --nostdlib link: ${missingOutput.join(", ") || "none"}; ` +
    `unexpected linked imports: ${unexpectedOutput.join(", ") || "none"}`,
  );
}

console.log(`runtime symbol routing: ok (${inputImports.size} manifest symbols)`);
