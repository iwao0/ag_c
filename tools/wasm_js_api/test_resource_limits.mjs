import { readFile } from "node:fs/promises";
import {
  AgcResourceLimitError,
  createToolchain,
} from "./agc-toolchain.js";

const compilerWasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const linkerWasmPath = process.argv[3] || "build/wasm_linker_selfhost/ag_wasm_link.wasm";
const compilerWasm = await readFile(compilerWasmPath);
const linkerWasm = await readFile(linkerWasmPath);
const runtimeObject = await readFile("build/libagc_runtime.o");

async function makeToolchain(options = {}) {
  return createToolchain({ compilerWasm, linkerWasm, runtimeObject, ...options });
}

function expectLimit(code, action) {
  try {
    action();
  } catch (err) {
    if (!(err instanceof AgcResourceLimitError) || err.code !== code) {
      throw new Error(`expected ${code}, got ${err?.name}: ${err?.code}: ${err?.message}`);
    }
    if (!Number.isSafeInteger(err.max) || !Number.isSafeInteger(err.actual) || err.actual <= err.max) {
      throw new Error(`resource error has invalid boundary metadata: ${JSON.stringify(err)}`);
    }
    return err;
  }
  throw new Error(`expected ${code}, but operation succeeded`);
}

const toolchain = await makeToolchain();
if (!Object.isFrozen(toolchain.resourceLimits) || toolchain.resourceLimits.maxDiagnostics !== 128 ||
    toolchain.resourceLimits.maxObjectBytes !== 16 * 1024 * 1024) {
  throw new Error(`resource defaults are not exposed or stable: ${JSON.stringify(toolchain.resourceLimits)}`);
}

const sourceBase = "int source_limit(void) { return 1; }\n";
const sourceLimit = Buffer.byteLength(sourceBase) + 2;
toolchain.compileObject(`${sourceBase} `, { limits: { maxSourceBytes: sourceLimit } });
toolchain.compileObject(`${sourceBase}  `, { limits: { maxSourceBytes: sourceLimit } });
expectLimit("AGC_LIMIT_MAX_SOURCE_BYTES", () =>
  toolchain.compileObject(`${sourceBase}   `, { limits: { maxSourceBytes: sourceLimit } }));

const countSources = [
  "int count_a(void) { return 1; }\n",
  "int count_b(void) { return 2; }\n",
  "int count_c(void) { return 3; }\n",
];
const sourceCountToolchain = await makeToolchain({ limits: { maxSources: 2 } });
sourceCountToolchain.compileLinkedWasm(countSources.slice(0, 1), {
  exports: ["count_a"], useStdlib: false,
});
sourceCountToolchain.compileLinkedWasm(countSources.slice(0, 2), {
  exports: ["count_a", "count_b"], useStdlib: false,
});
expectLimit("AGC_LIMIT_MAX_SOURCES", () =>
  sourceCountToolchain.compileLinkedWasm(countSources, { useStdlib: false }));

const totalSourceLimit = Buffer.byteLength(countSources[0]) + Buffer.byteLength(countSources[1]);
toolchain.compileLinkedWasm([countSources[0], countSources[1].slice(0, -1)], {
  exports: ["count_a", "count_b"], useStdlib: false,
  limits: { maxTotalSourceBytes: totalSourceLimit },
});
toolchain.compileLinkedWasm(countSources.slice(0, 2), {
  exports: ["count_a", "count_b"], useStdlib: false,
  limits: { maxTotalSourceBytes: totalSourceLimit },
});
expectLimit("AGC_LIMIT_MAX_TOTAL_SOURCE_BYTES", () =>
  toolchain.compileLinkedWasm([countSources[0], `${countSources[1]} `], {
    useStdlib: false,
    limits: { maxTotalSourceBytes: totalSourceLimit },
  }));

const headerMain = { name: "header-main.c", source: "int header_main(void) { return 0; }\n" };
toolchain.compileObject(headerMain, { headers: {}, limits: { maxHeaders: 1 } });
toolchain.compileObject(headerMain, {
  headers: { "one.h": "#define ONE 1\n" }, limits: { maxHeaders: 1 },
});
expectLimit("AGC_LIMIT_MAX_HEADERS", () => toolchain.compileObject(headerMain, {
  headers: { "one.h": "#define ONE 1\n", "two.h": "#define TWO 2\n" },
  limits: { maxHeaders: 1 },
}));

const headerText = "#define HEADER_VALUE 1\n  ";
const headerByteLimit = Buffer.byteLength(headerText) - 1;
toolchain.compileObject(headerMain, {
  headers: { "value.h": headerText.slice(0, -2) },
  limits: { maxHeaderBytes: headerByteLimit },
});
toolchain.compileObject(headerMain, {
  headers: { "value.h": headerText.slice(0, -1) },
  limits: { maxHeaderBytes: headerByteLimit },
});
expectLimit("AGC_LIMIT_MAX_HEADER_BYTES", () => toolchain.compileObject(headerMain, {
  headers: { "value.h": headerText }, limits: { maxHeaderBytes: headerByteLimit },
}));

const headerA = "#define A 1\n";
const headerB = "#define B 2\n";
const totalHeaderLimit = Buffer.byteLength(headerA) + Buffer.byteLength(headerB);
toolchain.compileObject(headerMain, {
  headers: { "a.h": headerA, "b.h": headerB.slice(0, -1) },
  limits: { maxTotalHeaderBytes: totalHeaderLimit },
});
toolchain.compileObject(headerMain, {
  headers: { "a.h": headerA, "b.h": headerB },
  limits: { maxTotalHeaderBytes: totalHeaderLimit },
});
expectLimit("AGC_LIMIT_MAX_TOTAL_HEADER_BYTES", () => toolchain.compileObject(headerMain, {
  headers: { "a.h": headerA, "b.h": `${headerB} ` },
  limits: { maxTotalHeaderBytes: totalHeaderLimit },
}));

const sizedSource = "int sized_output(void) { return 42; }\n";
const baselineObject = toolchain.compileObject(sizedSource);
toolchain.compileObject(sizedSource, { limits: { maxObjectBytes: baselineObject.length + 1 } });
const exactObject = toolchain.compileObject(sizedSource, {
  limits: { maxObjectBytes: baselineObject.length },
});
if (exactObject.length !== baselineObject.length) throw new Error("object size changed at exact limit");
expectLimit("AGC_LIMIT_MAX_OBJECT_BYTES", () => toolchain.compileObject(sizedSource, {
  limits: { maxObjectBytes: baselineObject.length - 1 },
}));

const baselineLinked = toolchain.compileLinkedWasm(sizedSource, {
  exports: ["sized_output"], useStdlib: false,
});
toolchain.compileLinkedWasm(sizedSource, {
  exports: ["sized_output"], useStdlib: false,
  limits: { maxLinkedWasmBytes: baselineLinked.length + 1 },
});
const exactLinked = toolchain.compileLinkedWasm(sizedSource, {
  exports: ["sized_output"], useStdlib: false,
  limits: { maxLinkedWasmBytes: baselineLinked.length },
});
if (exactLinked.length !== baselineLinked.length) throw new Error("linked Wasm size changed at exact limit");
expectLimit("AGC_LIMIT_MAX_LINKED_WASM_BYTES", () => toolchain.compileLinkedWasm(sizedSource, {
  exports: ["sized_output"], useStdlib: false,
  limits: { maxLinkedWasmBytes: baselineLinked.length - 1 },
}));
toolchain.compileLinkedWasm(sizedSource, {
  exports: ["sized_output"], useStdlib: false,
});

const warningSources = [
  "int warnings(void) { int a = 1.5; return a; }\n",
  "int warnings(void) { int a = 1.5; int b = 2.5; return a + b; }\n",
  "int warnings(void) { int a = 1.5; int b = 2.5; int c = 3.5; return a + b + c; }\n",
];
if (toolchain.compileObjectWithDiagnostics(warningSources[0], {
  limits: { maxDiagnostics: 2 },
}).diagnostics.length !== 1) {
  throw new Error("diagnostic count below the limit was not retained");
}
if (toolchain.compileObjectWithDiagnostics(warningSources[1], {
  limits: { maxDiagnostics: 2 },
}).diagnostics.length !== 2) {
  throw new Error("diagnostic count at the limit was not retained");
}
const countError = expectLimit("AGC_LIMIT_MAX_DIAGNOSTICS", () =>
  toolchain.compileObjectWithDiagnostics(warningSources[2], { limits: { maxDiagnostics: 2 } }));
if (countError.diagnostics.length !== 2) {
  throw new Error(`diagnostic storage grew past its count limit: ${countError.diagnostics.length}`);
}

const manyWarningDeclarations = Array.from(
  { length: 129 }, (_, index) => `int value_${index} = ${index}.5;`,
).join(" ");
const manyWarningSum = Array.from({ length: 129 }, (_, index) => `value_${index}`).join(" + ");
const manyWarningResult = toolchain.compileObjectWithDiagnostics(
  `int many_warnings(void) { ${manyWarningDeclarations} return ${manyWarningSum}; }\n`,
  { limits: { maxDiagnostics: 129 } },
);
if (manyWarningResult.diagnostics.length !== 129) {
  throw new Error(`diagnostic storage did not expand beyond the legacy cap: ${manyWarningResult.diagnostics.length}`);
}

toolchain.compileObjectWithDiagnostics(warningSources[1]);
const diagnosticBytes = Number(toolchain.compiler.instance.exports.agc_wasm_diagnostic_bytes());
if (diagnosticBytes <= 1) throw new Error(`invalid diagnostic byte count: ${diagnosticBytes}`);
toolchain.compileObjectWithDiagnostics(warningSources[0], {
  limits: { maxDiagnosticBytes: diagnosticBytes },
});
toolchain.compileObjectWithDiagnostics(warningSources[1], {
  limits: { maxDiagnosticBytes: diagnosticBytes },
});
expectLimit("AGC_LIMIT_MAX_DIAGNOSTIC_BYTES", () =>
  toolchain.compileObjectWithDiagnostics(warningSources[1], {
    limits: { maxDiagnosticBytes: diagnosticBytes - 1 },
  }));

const stderrChunks = [];
const cappedDiagnosticToolchain = await makeToolchain({
  limits: { maxDiagnostics: 1 },
  compilerOptions: { onStderr: (chunk) => stderrChunks.push(chunk) },
});
expectLimit("AGC_LIMIT_MAX_DIAGNOSTICS", () =>
  cappedDiagnosticToolchain.compileObjectWithDiagnostics(warningSources[2]));
const warningWrites = stderrChunks.join("").match(/W3010/g)?.length ?? 0;
if (warningWrites !== 1) {
  throw new Error(`diagnostics continued streaming after the limit: ${warningWrites}`);
}

console.log("ag_c wasm JS resource limits: ok");
