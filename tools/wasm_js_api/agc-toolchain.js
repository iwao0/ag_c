import { createCompiler } from "./agc-wasm.js?v=runtime-object";
import {
  AgcResourceLimitError,
  DEFAULT_AGC_RESOURCE_LIMITS,
  assertResourceLimit,
  resolveResourceLimits,
  utf8ByteLength,
} from "./agc-resource-limits.js";
import { createAgcRuntimeImports } from "./agc-runtime-imports.js?v=runtime-object";
import { createLinker } from "../wasm_obj_linker/ag-wasm-link.js?v=runtime-object";

const utf8Decoder = new TextDecoder();
const utf8Encoder = new TextEncoder();

export { AgcResourceLimitError } from "./agc-resource-limits.js";

export const AGC_CONTINUATION_STATUS = Object.freeze({
  INVALID: -1,
  NOT_STARTED: 0,
  SUSPENDED: 2,
  COMPLETED: 3,
});

async function loadBytes(source, label) {
  if (source === undefined || source === null) return null;
  if (source instanceof Uint8Array) return source;
  if (source instanceof ArrayBuffer) return new Uint8Array(source);
  if (typeof source === "string" || source instanceof URL) {
    if (typeof fetch !== "function") {
      throw new TypeError(`${label} URL loading requires fetch`);
    }
    const response = await fetch(source);
    if (!response.ok) throw new Error(`failed to fetch ${label}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
  }
  throw new TypeError(`${label} must be a URL, ArrayBuffer, or Uint8Array`);
}

function normalizeSources(sources, resourceLimits) {
  const isNamedSource = (source) => source && typeof source === "object" && !Array.isArray(source);
  const list = typeof sources === "string" || isNamedSource(sources) ? [sources] : sources;
  if (!Array.isArray(list) || list.length === 0) {
    throw new TypeError("sources must be a source or a non-empty source array");
  }
  assertResourceLimit("maxSources", list.length, resourceLimits.maxSources);
  const names = new Set();
  let totalSourceBytes = 0;
  for (const [i, source] of list.entries()) {
    if (typeof source === "string") {
      const sourceBytes = utf8ByteLength(source);
      assertResourceLimit("maxSourceBytes", sourceBytes, resourceLimits.maxSourceBytes);
      totalSourceBytes += sourceBytes;
      assertResourceLimit(
        "maxTotalSourceBytes", totalSourceBytes, resourceLimits.maxTotalSourceBytes,
      );
      continue;
    }
    if (!isNamedSource(source)) {
      throw new TypeError(`sources[${i}] must be a string or { name, source }`);
    }
    if (typeof source.name !== "string" || source.name.length === 0) {
      throw new TypeError(`sources[${i}].name must be a non-empty string`);
    }
    if (source.name.includes("\0")) {
      throw new TypeError(`sources[${i}].name must not contain NUL`);
    }
    if (typeof source.source !== "string") {
      throw new TypeError(`sources[${i}].source must be a string`);
    }
    const sourceBytes = utf8ByteLength(source.source);
    assertResourceLimit("maxSourceBytes", sourceBytes, resourceLimits.maxSourceBytes);
    totalSourceBytes += sourceBytes;
    assertResourceLimit(
      "maxTotalSourceBytes", totalSourceBytes, resourceLimits.maxTotalSourceBytes,
    );
    if (names.has(source.name)) {
      throw new TypeError(`duplicate source name: ${source.name}`);
    }
    names.add(source.name);
  }
  return list;
}

function freezeDiagnosticSnapshot(diagnostic, sourceId = diagnostic.sourceId) {
  return Object.freeze({
    ...diagnostic,
    sourceId,
    start: Object.freeze({ ...diagnostic.start }),
    end: Object.freeze({ ...diagnostic.end }),
    notes: Object.freeze(diagnostic.notes.map((note) => freezeDiagnosticSnapshot(note, sourceId))),
  });
}

function freezeDiagnosticSnapshots(diagnostics, sourceId) {
  return Object.freeze(diagnostics.map((diagnostic) =>
    freezeDiagnosticSnapshot(diagnostic, sourceId)));
}

function sourceNameOf(source) {
  return typeof source === "string" ? "input.c" : source.name;
}

function freezeSourceDiagnosticSnapshot(source, sourceId, diagnostics) {
  return Object.freeze({
    sourceId,
    sourceName: sourceNameOf(source),
    diagnostics,
  });
}

function hasOwn(obj, name) {
  return Object.prototype.hasOwnProperty.call(obj, name);
}

function normalizeStdinBytes(input) {
  if (typeof input === "string") return utf8Encoder.encode(input);
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError("stdio.stdin must be a string, ArrayBuffer, or Uint8Array");
}

function mergeExports(options, names) {
  const exports = options.exports ?? ["main"];
  if (!Array.isArray(exports)) throw new TypeError("exports must be an array");
  const merged = exports.slice();
  for (const name of names) {
    if (!merged.some((entry) =>
      (typeof entry === "string" ? entry : entry?.name) === name)) {
      merged.push(name);
    }
  }
  return { ...options, exports: merged };
}

function callRuntimeNumber(fn, ...args) {
  try {
    return Number(fn(...args.map((arg) => BigInt(arg))));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(fn(...args.map(Number)));
    }
    throw err;
  }
}

function callRuntimeVoid(fn, ...args) {
  try {
    fn(...args.map((arg) => BigInt(arg)));
  } catch (err) {
    if (err instanceof TypeError) {
      fn(...args.map(Number));
      return;
    }
    throw err;
  }
}

export async function createToolchain(options) {
  if (!options || options.compilerWasm === undefined || options.linkerWasm === undefined) {
    throw new TypeError("createToolchain requires compilerWasm and linkerWasm");
  }
  const resourceLimits = resolveResourceLimits(
    DEFAULT_AGC_RESOURCE_LIMITS,
    options.limits ?? {},
  );
  const compilerOptions = {
    ...(options.compilerOptions ?? {}),
    limits: resolveResourceLimits(
      resourceLimits,
      options.compilerOptions?.limits ?? {},
    ),
  };
  const compiler = await createCompiler(options.compilerWasm, compilerOptions);
  const linker = await createLinker(options.linkerWasm, options.linkerOptions);
  const runtimeObject = await loadBytes(options.runtimeObject, "runtimeObject");
  if (runtimeObject) {
    assertResourceLimit("maxObjectBytes", runtimeObject.length, resourceLimits.maxObjectBytes);
  }

  function compileAndLink(sources, linkOptions, withDiagnostics) {
    const {
      headers,
      headerLimits,
      continuation,
      limits: limitOverrides,
      maxOutputBytes: _ignoredMaxOutputBytes,
      ...linkerOptions
    } = linkOptions;
    const compileResourceLimits = resolveResourceLimits(resourceLimits, limitOverrides ?? {});
    const compileOptions = {
      limits: compileResourceLimits,
      ...(continuation === undefined ? {} : { continuation }),
      ...(headers === undefined && headerLimits === undefined
        ? {}
        : { headers: headers ?? {}, headerLimits }),
    };
    const normalizedSources = normalizeSources(sources, compileResourceLimits);
    const sourceDiagnostics = [];
    const objects = normalizedSources.map((source, i) => {
      try {
        if (!withDiagnostics) return compiler.compileObject(source, compileOptions);
        const result = compiler.compileObjectWithDiagnostics(source, compileOptions);
        assertResourceLimit(
          "maxObjectBytes", result.object.length, compileResourceLimits.maxObjectBytes,
          result.diagnostics,
        );
        const diagnostics = freezeDiagnosticSnapshots(result.diagnostics, i);
        sourceDiagnostics.push(freezeSourceDiagnosticSnapshot(source, i, diagnostics));
        return result.object;
      } catch (err) {
        err.sourceIndex = i;
        if (Array.isArray(err.diagnostics)) {
          err.diagnostics = freezeDiagnosticSnapshots(err.diagnostics, i);
          if (withDiagnostics) {
            err.sourceDiagnostics = Object.freeze([
              ...sourceDiagnostics,
              freezeSourceDiagnosticSnapshot(source, i, err.diagnostics),
            ]);
          }
        }
        err.message = `source ${i + 1}: ${err.message}`;
        throw err;
      }
    });
    if ((linkerOptions.useStdlib ?? true) && runtimeObject) {
      assertResourceLimit(
        "maxObjectBytes", runtimeObject.length, compileResourceLimits.maxObjectBytes,
      );
      objects.push(runtimeObject);
    }
    let wasm;
    const continuationExports = continuation ? [
      continuation.start ?? continuation.entry,
      continuation.resume ?? "__agc_continuation_resume",
      continuation.status ?? "__agc_continuation_status",
      continuation.result ?? "__agc_continuation_result",
    ] : [];
    const effectiveLinkerOptions = continuationExports.length
      ? mergeExports(linkerOptions, continuationExports)
      : linkerOptions;
    try {
      wasm = linker.link(objects, {
        ...effectiveLinkerOptions,
        maxOutputBytes: compileResourceLimits.maxLinkedWasmBytes,
      });
    } catch (err) {
      if (err?.code !== "AGC_LIMIT_MAX_LINKED_WASM_BYTES") throw err;
      const diagnostics = withDiagnostics
        ? Object.freeze(sourceDiagnostics.flatMap((entry) => entry.diagnostics))
        : Object.freeze([]);
      const resourceError = new AgcResourceLimitError(
        "maxLinkedWasmBytes",
        compileResourceLimits.maxLinkedWasmBytes,
        err.actual ?? compileResourceLimits.maxLinkedWasmBytes + 1,
        diagnostics,
      );
      if (withDiagnostics) resourceError.sourceDiagnostics = Object.freeze(sourceDiagnostics);
      throw resourceError;
    }
    assertResourceLimit(
      "maxLinkedWasmBytes", wasm.length, compileResourceLimits.maxLinkedWasmBytes,
    );
    if (!withDiagnostics) return wasm;
    return Object.freeze({
      wasm,
      diagnostics: Object.freeze(sourceDiagnostics.flatMap((entry) => entry.diagnostics)),
      sourceDiagnostics: Object.freeze(sourceDiagnostics),
    });
  }

  function compileLinkedWasm(sources, linkOptions = {}) {
    return compileAndLink(sources, linkOptions, false);
  }

  function compileLinkedWasmWithDiagnostics(sources, linkOptions = {}) {
    return compileAndLink(sources, linkOptions, true);
  }

  async function instantiateLinkedWasm(sources, linkOptions = {}, imports = {}) {
    const stdioOptions = imports.stdio || {};
    const shouldInjectRuntimeStdin =
      hasOwn(stdioOptions, "stdin") && (linkOptions.useStdlib ?? true) && runtimeObject;
    const stdinBytes = shouldInjectRuntimeStdin ? normalizeStdinBytes(stdioOptions.stdin) : null;
    const effectiveLinkOptions = shouldInjectRuntimeStdin ? mergeExports(linkOptions, [
      "__agc_runtime_malloc",
      "__agc_runtime_free",
      "__agc_runtime_stdin_capacity",
      "__agc_runtime_stdin_write",
    ]) : linkOptions;
    const wasm = compileLinkedWasm(sources, effectiveLinkOptions);
    let memory = null;
    const runtimeImports = createAgcRuntimeImports({
      ...imports,
      stdio: {
        ...(imports.stdio || {}),
        getMemory: () => memory,
      },
    });
    const result = await WebAssembly.instantiate(wasm, runtimeImports);
    memory = result.instance.exports.memory;
    if (stdinBytes) {
      const capacityFn = result.instance.exports.__agc_runtime_stdin_capacity;
      const writeFn = result.instance.exports.__agc_runtime_stdin_write;
      const malloc = result.instance.exports.__agc_runtime_malloc;
      const free = result.instance.exports.__agc_runtime_free;
      if (!(memory instanceof WebAssembly.Memory) ||
          typeof capacityFn !== "function" ||
          typeof writeFn !== "function" ||
          typeof malloc !== "function" ||
          typeof free !== "function") {
        throw new Error("linked runtime does not export stdin injection helpers");
      }
      const capacity = callRuntimeNumber(capacityFn);
      if (stdinBytes.length > capacity) {
        throw new RangeError(`stdio.stdin is ${stdinBytes.length} bytes; runtime capacity is ${capacity} bytes`);
      }
      const ptr = callRuntimeNumber(malloc, Math.max(1, stdinBytes.length));
      if (!ptr || ptr + stdinBytes.length > memory.buffer.byteLength) {
        throw new Error("linked runtime malloc failed for stdin");
      }
      try {
        new Uint8Array(memory.buffer, ptr, stdinBytes.length).set(stdinBytes);
        const written = callRuntimeNumber(writeFn, ptr, stdinBytes.length);
        if (written !== stdinBytes.length) {
          throw new Error(`linked runtime accepted ${written} of ${stdinBytes.length} stdin bytes`);
        }
      } finally {
        callRuntimeVoid(free, ptr);
      }
    }
    function readExportBuffer(ptrName, lenName) {
      const ptrFn = result.instance.exports[ptrName];
      const lenFn = result.instance.exports[lenName];
      if (!(memory instanceof WebAssembly.Memory) ||
          typeof ptrFn !== "function" ||
          typeof lenFn !== "function") {
        return "";
      }
      const ptr = Number(ptrFn());
      const len = Number(lenFn());
      if (ptr <= 0 || len <= 0 || ptr + len > memory.buffer.byteLength) return "";
      return utf8Decoder.decode(new Uint8Array(memory.buffer, ptr, len));
    }
    return {
      wasm,
      module: result.module,
      instance: result.instance,
      readStdout: () => readExportBuffer("__agc_runtime_stdout_ptr", "__agc_runtime_stdout_len"),
      readStderr: () => readExportBuffer("__agc_runtime_stderr_ptr", "__agc_runtime_stderr_len"),
    };
  }

  return {
    compiler,
    linker,
    compileWat: (source, compileOptions) => compiler.compileWat(source, compileOptions),
    compileWatWithDiagnostics: (source, compileOptions) =>
      compiler.compileWatWithDiagnostics(source, compileOptions),
    compileObject: (source, compileOptions) => compiler.compileObject(source, compileOptions),
    compileObjectWithDiagnostics: (source, compileOptions) =>
      compiler.compileObjectWithDiagnostics(source, compileOptions),
    compileLinkedWasm,
    compileLinkedWasmWithDiagnostics,
    instantiateLinkedWasm,
    resourceLimits,
  };
}

export default createToolchain;
