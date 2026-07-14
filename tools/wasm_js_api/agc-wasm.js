import { createAgcRuntimeImports } from "./agc-runtime-imports.js?v=runtime-object";
import {
  AgcResourceLimitError,
  DEFAULT_AGC_RESOURCE_LIMITS,
  assertResourceLimit,
  resolveResourceLimits,
  utf8ByteLength,
} from "./agc-resource-limits.js";

export { AgcResourceLimitError } from "./agc-resource-limits.js";

const DEFAULT_SOURCE_PTR = 393216;
const DEFAULT_SOURCE_CAP = 32768;
const DEFAULT_OUTPUT_PTR = DEFAULT_SOURCE_PTR + DEFAULT_SOURCE_CAP;
const DEFAULT_OUTPUT_CAP = 98304;
const DEFAULT_INITIAL_OUTPUT_CAP = 131072;
const DEFAULT_WAT_OUTPUT_LIMIT = 16 * 1024 * 1024;

function asBytes(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError("wasm must be an ArrayBuffer or Uint8Array");
}

async function instantiateFromSource(wasmSource, imports = {}) {
  if (wasmSource instanceof WebAssembly.Module) {
    return await WebAssembly.instantiate(wasmSource, imports);
  }
  if (typeof wasmSource === "string" || wasmSource instanceof URL) {
    if (typeof WebAssembly.instantiateStreaming === "function" && typeof fetch === "function") {
      try {
        const result = await WebAssembly.instantiateStreaming(fetch(wasmSource), imports);
        return result.instance;
      } catch (_) {
        // Fall back for file servers that do not send application/wasm.
      }
    }
    if (typeof fetch !== "function") {
      throw new TypeError("URL wasm loading requires fetch; pass an ArrayBuffer in this environment");
    }
    const response = await fetch(wasmSource);
    if (!response.ok) throw new Error(`failed to fetch wasm: ${response.status}`);
    const bytes = await response.arrayBuffer();
    return (await WebAssembly.instantiate(bytes, imports)).instance;
  }
  return (await WebAssembly.instantiate(asBytes(wasmSource), imports)).instance;
}

function callCompile(fn, sourcePtr, sourceNamePtr, headerPtr, headerLen,
                     headerLimits, outputPtr, outputCap) {
  let args;
  if (headerPtr) {
    args = [
      sourcePtr, sourceNamePtr, headerPtr, headerLen,
      headerLimits.maxFiles, headerLimits.maxFileBytes,
      headerLimits.maxTotalBytes, headerLimits.maxIncludeDepth,
      outputPtr, outputCap,
    ];
  } else if (sourceNamePtr) {
    args = [sourcePtr, sourceNamePtr, outputPtr, outputCap];
  } else {
    args = [sourcePtr, outputPtr, outputCap];
  }
  try {
    return Number(fn(...args.map((arg) => BigInt(arg))));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(fn(...args));
    }
    throw err;
  }
}

function positiveInt(value, label, max = 0x7fffffff) {
  if (!Number.isSafeInteger(value) || value <= 0 || value > max) {
    throw new RangeError(`${label} must be an integer from 1 to ${max}`);
  }
  return value;
}

function prepareVirtualHeaders(options, encoder, resourceLimits) {
  if (!options || (options.headers === undefined && options.headerLimits === undefined)) return null;
  const headers = options.headers ?? {};
  if (!headers || typeof headers !== "object" || Array.isArray(headers)) {
    throw new TypeError("headers must be an object mapping paths to source strings");
  }
  const limitsInput = options.headerLimits ?? {};
  if (!limitsInput || typeof limitsInput !== "object" || Array.isArray(limitsInput)) {
    throw new TypeError("headerLimits must be an object");
  }
  const limits = {
    maxFiles: positiveInt(limitsInput.maxFiles ?? resourceLimits.maxHeaders, "headerLimits.maxFiles"),
    maxFileBytes: positiveInt(
      limitsInput.maxFileBytes ?? resourceLimits.maxHeaderBytes,
      "headerLimits.maxFileBytes",
    ),
    maxTotalBytes: positiveInt(
      limitsInput.maxTotalBytes ?? resourceLimits.maxTotalHeaderBytes,
      "headerLimits.maxTotalBytes",
    ),
    maxIncludeDepth: positiveInt(
      limitsInput.maxIncludeDepth ?? resourceLimits.maxIncludeDepth,
      "headerLimits.maxIncludeDepth", 64,
    ),
  };
  const rawEntries = Object.entries(headers);
  if (limitsInput.maxFiles === undefined) {
    assertResourceLimit("maxHeaders", rawEntries.length, limits.maxFiles);
  }
  let totalSourceBytes = 0;
  const entries = rawEntries.map(([path, source]) => {
    if (path.includes("\0")) throw new TypeError("virtual header path must not contain NUL");
    if (typeof source !== "string") {
      throw new TypeError(`virtual header ${JSON.stringify(path)} must contain a string`);
    }
    if (source.includes("\0")) {
      throw new TypeError(`virtual header ${JSON.stringify(path)} must not contain NUL`);
    }
    const sourceByteLength = utf8ByteLength(source);
    if (limitsInput.maxFileBytes === undefined) {
      assertResourceLimit("maxHeaderBytes", sourceByteLength, limits.maxFileBytes);
    }
    totalSourceBytes += sourceByteLength;
    if (limitsInput.maxTotalBytes === undefined) {
      assertResourceLimit("maxTotalHeaderBytes", totalSourceBytes, limits.maxTotalBytes);
    }
    return { path: encoder.encode(path), source: encoder.encode(source) };
  });
  let byteLength = 4;
  for (const entry of entries) {
    byteLength += 8 + entry.path.length + 1 + entry.source.length + 1;
    if (!Number.isSafeInteger(byteLength) || byteLength > 0x7fffffff) {
      throw new RangeError("virtual header bundle exceeds Wasm32 addressable size");
    }
  }
  const bytes = new Uint8Array(byteLength);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, entries.length, true);
  let offset = 4;
  for (const entry of entries) {
    view.setUint32(offset, entry.path.length, true);
    view.setUint32(offset + 4, entry.source.length, true);
    offset += 8;
    bytes.set(entry.path, offset);
    offset += entry.path.length + 1;
    bytes.set(entry.source, offset);
    offset += entry.source.length + 1;
  }
  return { bytes, limits };
}

function normalizeCompileInput(input) {
  if (typeof input === "string") return { source: input, name: null };
  if (!input || typeof input !== "object" || Array.isArray(input)) {
    throw new TypeError("source must be a string or { name, source }");
  }
  if (typeof input.name !== "string" || input.name.length === 0) {
    throw new TypeError("source.name must be a non-empty string");
  }
  if (input.name.includes("\0")) {
    throw new TypeError("source.name must not contain NUL");
  }
  if (typeof input.source !== "string") {
    throw new TypeError("source.source must be a string");
  }
  return { source: input.source, name: input.name };
}

function callPtrFunc(fn, arg) {
  try {
    return Number(fn(BigInt(arg)));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(fn(arg));
    }
    throw err;
  }
}

function callVoidPtrFunc(fn, arg) {
  try {
    fn(BigInt(arg));
  } catch (err) {
    if (err instanceof TypeError) {
      fn(arg);
      return;
    }
    throw err;
  }
}

function callNoArgNumberFunc(fn) {
  return Number(fn());
}

function callNoArgVoidFunc(fn) {
  fn();
}

function callTwoArgNumberFunc(fn, first, second) {
  try {
    return Number(fn(BigInt(first), BigInt(second)));
  } catch (err) {
    if (err instanceof TypeError) return Number(fn(first, second));
    throw err;
  }
}

function callNumberFunc(fn, args) {
  try {
    return Number(fn(...args.map((arg) => BigInt(arg))));
  } catch (err) {
    if (err instanceof TypeError) return Number(fn(...args));
    throw err;
  }
}

function ensureMemoryRange(memory, ptr, len, label) {
  if (ptr < 0 || len < 0 || ptr + len > memory.buffer.byteLength) {
    throw new RangeError(`${label} buffer is outside wasm memory`);
  }
}

function compileErrorMessage(code, outputCap) {
  if (code === -1) return "ag_c wasm compile failed: invalid buffer arguments";
  if (code === -2) return `ag_c wasm output exceeded ${outputCap} bytes`;
  if (code === -3) return "ag_c wasm compile failed while building IR";
  if (code === -4) return "ag_c wasm compile produced no output";
  return `ag_c wasm compile failed with code ${code}`;
}

export async function createCompiler(wasmSource, options = {}) {
  const compilerResourceLimits = resolveResourceLimits(
    DEFAULT_AGC_RESOURCE_LIMITS,
    options.limits ?? {},
  );
  const decoder = new TextDecoder();
  let callbackMemory = null;
  const stdoutChunks = [];
  const stderrChunks = [];
  let terminationNotified = false;
  function readCallbackBytes(ptr, len) {
    const n = Number(len);
    const p = Number(ptr);
    if (!callbackMemory || p <= 0 || n <= 0 || p + n > callbackMemory.buffer.byteLength) return "";
    return decoder.decode(new Uint8Array(callbackMemory.buffer, p, n));
  }
  const envImports = {
    __agc_runtime_stdout_write(ptr, len) {
      const text = readCallbackBytes(ptr, len);
      if (!text) return;
      if (typeof options.onStdout === "function") {
        options.onStdout(text);
      } else {
        stdoutChunks.push(text);
      }
    },
    __agc_runtime_stderr_write(ptr, len) {
      const text = readCallbackBytes(ptr, len);
      if (!text) return;
      if (typeof options.onStderr === "function") {
        options.onStderr(text);
      } else {
        stderrChunks.push(text);
      }
    },
  };
  const imports = createAgcRuntimeImports({ env: envImports });
  const instance = await instantiateFromSource(wasmSource, imports);
  const memory = instance.exports.memory;
  callbackMemory = memory;
  const compileWatExport = instance.exports.agc_wasm_compile_wat;
  const compileObjectExport = instance.exports.agc_wasm_compile_object;
  const compileWatNamedExport = instance.exports.agc_wasm_compile_wat_named;
  const compileObjectNamedExport = instance.exports.agc_wasm_compile_object_named;
  const compileWatVirtualExport = instance.exports.agc_wasm_compile_wat_virtual;
  const compileObjectVirtualExport = instance.exports.agc_wasm_compile_object_virtual;
  const continuationOptionsExport = instance.exports.agc_wasm_set_continuation_options;
  const diagnosticExports = {
    apiVersion: instance.exports.agc_wasm_diagnostic_api_version,
    count: instance.exports.agc_wasm_diagnostic_count,
    severity: instance.exports.agc_wasm_diagnostic_severity,
    codePtr: instance.exports.agc_wasm_diagnostic_code_ptr,
    messagePtr: instance.exports.agc_wasm_diagnostic_message_ptr,
    sourceNamePtr: instance.exports.agc_wasm_diagnostic_source_name_ptr,
    startLine: instance.exports.agc_wasm_diagnostic_start_line,
    startColumn: instance.exports.agc_wasm_diagnostic_start_column,
    startOffset: instance.exports.agc_wasm_diagnostic_start_offset,
    endLine: instance.exports.agc_wasm_diagnostic_end_line,
    endColumn: instance.exports.agc_wasm_diagnostic_end_column,
    endOffset: instance.exports.agc_wasm_diagnostic_end_offset,
  };
  const diagnosticLimitExports = {
    set: instance.exports.agc_wasm_diagnostic_set_limits,
    bytes: instance.exports.agc_wasm_diagnostic_bytes,
    kind: instance.exports.agc_wasm_diagnostic_limit_kind,
  };
  const stdoutPtrExport = instance.exports.__agc_runtime_stdout_ptr;
  const stdoutLenExport = instance.exports.__agc_runtime_stdout_len;
  const stderrPtrExport = instance.exports.__agc_runtime_stderr_ptr;
  const stderrLenExport = instance.exports.__agc_runtime_stderr_len;
  const stderrResetExport = instance.exports.__agc_runtime_stderr_reset;
  const terminationKindExport = instance.exports.__agc_runtime_termination_kind;
  const terminationStatusExport = instance.exports.__agc_runtime_termination_status;
  const malloc = instance.exports.malloc;
  const free = instance.exports.free;
  if (!(memory instanceof WebAssembly.Memory)) {
    throw new Error("ag_c wasm module does not export memory");
  }
  if (typeof compileWatExport !== "function") {
    throw new Error("ag_c wasm module does not export agc_wasm_compile_wat");
  }

  const sourceCap = options.sourceCap ?? DEFAULT_SOURCE_CAP;
  const outputCap = options.outputCap ?? DEFAULT_OUTPUT_CAP;
  const initialOutputCap = options.initialOutputCap ?? DEFAULT_INITIAL_OUTPUT_CAP;
  const useHeapBuffers = options.useHeapBuffers ?? (
    options.sourcePtr === undefined &&
    options.outputPtr === undefined &&
    typeof malloc === "function" &&
    typeof free === "function"
  );
  if (useHeapBuffers && (typeof malloc !== "function" || typeof free !== "function")) {
    throw new Error("heap buffer mode requires malloc/free exports");
  }
  if (initialOutputCap <= 0) {
    throw new RangeError("initialOutputCap must be positive");
  }
  let sourcePtr = options.sourcePtr ?? DEFAULT_SOURCE_PTR;
  let outputPtr = options.outputPtr ?? DEFAULT_OUTPUT_PTR;
  if (!useHeapBuffers && options.sourcePtr === undefined && options.outputPtr === undefined &&
      typeof malloc === "function" && typeof free === "function") {
    sourcePtr = callPtrFunc(malloc, sourceCap);
    if (!sourcePtr) throw new Error("ag_c wasm malloc failed for fixed source buffer");
    outputPtr = callPtrFunc(malloc, outputCap);
    if (!outputPtr) {
      callVoidPtrFunc(free, sourcePtr);
      throw new Error("ag_c wasm malloc failed for fixed output buffer");
    }
    ensureMemoryRange(memory, sourcePtr, sourceCap, "fixed source");
    ensureMemoryRange(memory, outputPtr, outputCap, "fixed output");
  }
  const encoder = new TextEncoder();

  function continuationNames(input) {
    if (input === undefined) return null;
    if (!input || typeof input !== "object" || Array.isArray(input)) {
      throw new TypeError("continuation must be an object");
    }
    const names = {
      entry: input.entry,
      frameCondition: input.frameCondition,
      start: input.start ?? input.entry,
      resume: input.resume ?? "__agc_continuation_resume",
      status: input.status ?? "__agc_continuation_status",
      result: input.result ?? "__agc_continuation_result",
    };
    for (const [key, value] of Object.entries(names)) {
      if (typeof value !== "string" || value.length === 0 || value.includes("\0")) {
        throw new TypeError(`continuation.${key} must be a non-empty string without NUL`);
      }
    }
    return names;
  }

  function configureContinuation(input) {
    const names = continuationNames(input);
    if (!names) return () => {};
    if (typeof continuationOptionsExport !== "function") {
      throw new Error("ag_c wasm module does not support continuation compilation");
    }
    if (typeof malloc !== "function" || typeof free !== "function") {
      throw new Error("continuation compilation requires malloc/free exports");
    }
    const allocations = [];
    try {
      const pointers = Object.values(names).map((name) => {
        const bytes = encoder.encode(`${name}\0`);
        const ptr = callPtrFunc(malloc, bytes.length);
        if (!ptr) throw new Error("ag_c wasm malloc failed for continuation option");
        allocations.push(ptr);
        ensureMemoryRange(memory, ptr, bytes.length, "continuation option");
        new Uint8Array(memory.buffer).set(bytes, ptr);
        return ptr;
      });
      if (callNumberFunc(continuationOptionsExport, pointers) !== 0) {
        throw new Error("ag_c wasm rejected continuation options");
      }
    } catch (err) {
      for (const ptr of allocations) callVoidPtrFunc(free, ptr);
      throw err;
    }
    return () => {
      callNumberFunc(continuationOptionsExport, [0, 0, 0, 0, 0, 0]);
      for (const ptr of allocations) callVoidPtrFunc(free, ptr);
    };
  }

  function configureDiagnosticLimits(resourceLimits) {
    if (typeof diagnosticLimitExports.set !== "function") {
      if (resourceLimits.maxDiagnostics !== DEFAULT_AGC_RESOURCE_LIMITS.maxDiagnostics ||
          resourceLimits.maxDiagnosticBytes !== DEFAULT_AGC_RESOURCE_LIMITS.maxDiagnosticBytes) {
        throw new Error("ag_c wasm module does not support diagnostic resource limits");
      }
      return;
    }
    const rc = callTwoArgNumberFunc(
      diagnosticLimitExports.set,
      resourceLimits.maxDiagnostics,
      resourceLimits.maxDiagnosticBytes,
    );
    if (rc !== 0) throw new Error("ag_c wasm rejected diagnostic resource limits");
  }

  function resetDiagnostics() {
    stdoutChunks.length = 0;
    stderrChunks.length = 0;
    terminationNotified = false;
    if (typeof stderrResetExport === "function") callNoArgVoidFunc(stderrResetExport);
  }

  function readFallbackBuffer(ptrExport, lenExport) {
    if (typeof ptrExport !== "function" || typeof lenExport !== "function") return "";
    const ptr = callNoArgNumberFunc(ptrExport);
    const len = callNoArgNumberFunc(lenExport);
    if (ptr <= 0 || len <= 0 || ptr + len > memory.buffer.byteLength) return "";
    return decoder.decode(new Uint8Array(memory.buffer, ptr, len));
  }

  function readStdout() {
    return stdoutChunks.join("") || readFallbackBuffer(stdoutPtrExport, stdoutLenExport);
  }

  function readStderrText() {
    return (stderrChunks.join("") || readFallbackBuffer(stderrPtrExport, stderrLenExport)).trim();
  }

  function readCString(ptr, maxBytes = 1024 * 1024) {
    ptr = Number(ptr);
    if (ptr <= 0 || ptr >= memory.buffer.byteLength) return "";
    const bytes = new Uint8Array(memory.buffer);
    const limit = Math.min(bytes.length, ptr + maxBytes);
    let end = ptr;
    while (end < limit && bytes[end] !== 0) end++;
    return decoder.decode(bytes.subarray(ptr, end));
  }

  function diagnosticExportAvailable() {
    return Object.values(diagnosticExports).every((value) => typeof value === "function") &&
      callNoArgNumberFunc(diagnosticExports.apiVersion) === 1;
  }

  function freezeDiagnostic(diagnostic) {
    return Object.freeze({
      ...diagnostic,
      start: Object.freeze({ ...diagnostic.start }),
      end: Object.freeze({ ...diagnostic.end }),
      notes: Object.freeze(diagnostic.notes.map(freezeDiagnostic)),
    });
  }

  function freezeDiagnostics(diagnostics) {
    return Object.freeze(diagnostics.map(freezeDiagnostic));
  }

  function readStructuredDiagnostics() {
    if (!diagnosticExportAvailable()) return Object.freeze([]);
    const count = callNoArgNumberFunc(diagnosticExports.count);
    const diagnostics = [];
    for (let index = 0; index < count; index++) {
      const readNumber = (fn) => callPtrFunc(fn, index);
      const severityNo = readNumber(diagnosticExports.severity);
      diagnostics.push({
        severity: severityNo === 1 ? "error" : severityNo === 2 ? "warning" : "note",
        code: readCString(readNumber(diagnosticExports.codePtr)),
        message: readCString(readNumber(diagnosticExports.messagePtr)),
        sourceId: 0,
        sourceName: readCString(readNumber(diagnosticExports.sourceNamePtr)),
        start: {
          line: readNumber(diagnosticExports.startLine),
          column: readNumber(diagnosticExports.startColumn),
          offset: readNumber(diagnosticExports.startOffset),
        },
        end: {
          line: readNumber(diagnosticExports.endLine),
          column: readNumber(diagnosticExports.endColumn),
          offset: readNumber(diagnosticExports.endOffset),
        },
        notes: [],
      });
    }
    return freezeDiagnostics(diagnostics);
  }

  function diagnosticResourceLimitError(resourceLimits, diagnostics = readStructuredDiagnostics()) {
    if (typeof diagnosticLimitExports.kind !== "function") return null;
    const kind = callNoArgNumberFunc(diagnosticLimitExports.kind);
    if (kind === 1) {
      return new AgcResourceLimitError(
        "maxDiagnostics",
        resourceLimits.maxDiagnostics,
        resourceLimits.maxDiagnostics + 1,
        diagnostics,
      );
    }
    if (kind === 2) {
      return new AgcResourceLimitError(
        "maxDiagnosticBytes",
        resourceLimits.maxDiagnosticBytes,
        resourceLimits.maxDiagnosticBytes + 1,
        diagnostics,
      );
    }
    return null;
  }

  function throwIfDiagnosticResourceLimitExceeded(resourceLimits) {
    const error = diagnosticResourceLimitError(resourceLimits);
    if (error) throw error;
  }

  function readTermination() {
    if (typeof terminationKindExport !== "function" || typeof terminationStatusExport !== "function") {
      return null;
    }
    const kindNo = callNoArgNumberFunc(terminationKindExport);
    if (kindNo === 0) return null;
    return {
      kind: kindNo === 1 ? "exit" : kindNo === 2 ? "abort" : "unknown",
      status: callNoArgNumberFunc(terminationStatusExport),
    };
  }

  function notifyTermination() {
    if (terminationNotified) return;
    const event = readTermination();
    if (!event) return;
    terminationNotified = true;
    if (typeof options.onTerminate === "function") options.onTerminate(event);
    if (event.kind === "exit" && typeof options.onExit === "function") options.onExit(event.status);
    if (event.kind === "abort" && typeof options.onAbort === "function") options.onAbort(event.status);
  }

  function throwCompileFailure(errOrCode, outputCap, resourceLimits) {
    notifyTermination();
    const diagnostics = readStructuredDiagnostics();
    const resourceError = diagnosticResourceLimitError(resourceLimits, diagnostics);
    if (resourceError) throw resourceError;
    const attachDiagnostics = (error) => {
      error.diagnostics = diagnostics;
      return error;
    };
    const diag = readStderrText();
    if (diag) throw attachDiagnostics(new Error(diag));
    const termination = readTermination();
    if (termination) {
      if (termination.kind === "exit") {
        throw attachDiagnostics(new Error(`ag_c wasm exited with status ${termination.status}`));
      }
      if (termination.kind === "abort") throw attachDiagnostics(new Error("ag_c wasm aborted"));
    }
    if (typeof errOrCode === "number") {
      throw attachDiagnostics(new Error(compileErrorMessage(errOrCode, outputCap)));
    }
    if (errOrCode && typeof errOrCode === "object") errOrCode.diagnostics = diagnostics;
    throw errOrCode;
  }

  function compileWithFixedBuffers(sourceBytes, sourceNameBytes, virtualHeaders,
                                   compileFn, asText, resourceLimits,
                                   maxOutputBytes, outputLimitName) {
    configureDiagnosticLimits(resourceLimits);
    if (sourceBytes.length > sourceCap) {
      throw new RangeError(`source is ${sourceBytes.length} bytes; max is ${sourceCap}`);
    }
    if (sourcePtr + sourceCap > memory.buffer.byteLength) {
      throw new RangeError("configured source buffer is outside wasm memory");
    }
    const effectiveOutputCap = Math.min(outputCap, maxOutputBytes);
    if (outputPtr + effectiveOutputCap > memory.buffer.byteLength) {
      throw new RangeError("configured output buffer is outside wasm memory");
    }

    let mem = new Uint8Array(memory.buffer);
    mem.fill(0, sourcePtr, sourcePtr + sourceCap);
    mem.fill(0, outputPtr, outputPtr + effectiveOutputCap);
    mem.set(sourceBytes, sourcePtr);

    let sourceNameAlloc = 0;
    let headerAlloc = 0;
    try {
      if (sourceNameBytes) {
        if (typeof malloc !== "function" || typeof free !== "function") {
          throw new Error("named source compilation requires malloc/free exports");
        }
        sourceNameAlloc = callPtrFunc(malloc, sourceNameBytes.length);
        if (!sourceNameAlloc) throw new Error("ag_c wasm malloc failed for source name");
        ensureMemoryRange(memory, sourceNameAlloc, sourceNameBytes.length, "source name");
        new Uint8Array(memory.buffer).set(sourceNameBytes, sourceNameAlloc);
      }
      if (virtualHeaders) {
        if (typeof malloc !== "function" || typeof free !== "function") {
          throw new Error("virtual header compilation requires malloc/free exports");
        }
        headerAlloc = callPtrFunc(malloc, virtualHeaders.bytes.length);
        if (!headerAlloc) throw new Error("ag_c wasm malloc failed for virtual headers");
        ensureMemoryRange(memory, headerAlloc, virtualHeaders.bytes.length, "virtual headers");
        new Uint8Array(memory.buffer).set(virtualHeaders.bytes, headerAlloc);
      }
      resetDiagnostics();
      let n;
      try {
        n = callCompile(
          compileFn, sourcePtr, sourceNameAlloc, headerAlloc,
          virtualHeaders?.bytes.length ?? 0, virtualHeaders?.limits,
          outputPtr, effectiveOutputCap,
        );
      } catch (err) {
        throwCompileFailure(err, effectiveOutputCap, resourceLimits);
      }
      if (n === -2 && outputLimitName && effectiveOutputCap === maxOutputBytes) {
        throw new AgcResourceLimitError(
          outputLimitName, maxOutputBytes, maxOutputBytes + 1,
          readStructuredDiagnostics(),
        );
      }
      if (n < 0) throwCompileFailure(n, effectiveOutputCap, resourceLimits);
      throwIfDiagnosticResourceLimitExceeded(resourceLimits);

      mem = new Uint8Array(memory.buffer);
      const bytes = mem.slice(outputPtr, outputPtr + n);
      return asText ? decoder.decode(bytes) : bytes;
    } finally {
      if (headerAlloc) callVoidPtrFunc(free, headerAlloc);
      if (sourceNameAlloc) callVoidPtrFunc(free, sourceNameAlloc);
    }
  }

  function compileWithHeapBuffers(sourceBytes, sourceNameBytes, virtualHeaders,
                                  compileFn, asText, resourceLimits,
                                  maxOutputBytes, outputLimitName) {
    configureDiagnosticLimits(resourceLimits);
    const sourceAlloc = callPtrFunc(malloc, sourceBytes.length);
    if (!sourceAlloc) throw new Error("ag_c wasm malloc failed for source buffer");
    let sourceNameAlloc = 0;
    let headerAlloc = 0;
    let outputAlloc = 0;
    let cap = Math.min(initialOutputCap, maxOutputBytes);
    try {
      let mem = new Uint8Array(memory.buffer);
      ensureMemoryRange(memory, sourceAlloc, sourceBytes.length, "source");
      mem.set(sourceBytes, sourceAlloc);
      if (sourceNameBytes) {
        sourceNameAlloc = callPtrFunc(malloc, sourceNameBytes.length);
        if (!sourceNameAlloc) throw new Error("ag_c wasm malloc failed for source name");
        ensureMemoryRange(memory, sourceNameAlloc, sourceNameBytes.length, "source name");
        new Uint8Array(memory.buffer).set(sourceNameBytes, sourceNameAlloc);
      }
      if (virtualHeaders) {
        headerAlloc = callPtrFunc(malloc, virtualHeaders.bytes.length);
        if (!headerAlloc) throw new Error("ag_c wasm malloc failed for virtual headers");
        ensureMemoryRange(memory, headerAlloc, virtualHeaders.bytes.length, "virtual headers");
        new Uint8Array(memory.buffer).set(virtualHeaders.bytes, headerAlloc);
      }

      for (;;) {
        outputAlloc = callPtrFunc(malloc, cap);
        if (!outputAlloc) throw new Error("ag_c wasm malloc failed for output buffer");
        mem = new Uint8Array(memory.buffer);
        ensureMemoryRange(memory, outputAlloc, cap, "output");
        mem.fill(0, outputAlloc, outputAlloc + cap);
        resetDiagnostics();
        let n;
        try {
          n = callCompile(
            compileFn, sourceAlloc, sourceNameAlloc, headerAlloc,
            virtualHeaders?.bytes.length ?? 0, virtualHeaders?.limits,
            outputAlloc, cap,
          );
        } catch (err) {
          throwCompileFailure(err, cap, resourceLimits);
        }
        if (n === -2) {
          callVoidPtrFunc(free, outputAlloc);
          outputAlloc = 0;
          if (cap >= maxOutputBytes) {
            if (outputLimitName) {
              throw new AgcResourceLimitError(
                outputLimitName, maxOutputBytes, maxOutputBytes + 1,
                readStructuredDiagnostics(),
              );
            }
            throw new RangeError(`ag_c wasm output exceeded ${maxOutputBytes} bytes`);
          }
          cap = Math.min(cap * 2, maxOutputBytes);
          continue;
        }
        if (n < 0) throwCompileFailure(n, cap, resourceLimits);
        throwIfDiagnosticResourceLimitExceeded(resourceLimits);
        mem = new Uint8Array(memory.buffer);
        const bytes = mem.slice(outputAlloc, outputAlloc + n);
        return asText ? decoder.decode(bytes) : bytes;
      }
    } finally {
      if (outputAlloc) callVoidPtrFunc(free, outputAlloc);
      if (headerAlloc) callVoidPtrFunc(free, headerAlloc);
      if (sourceNameAlloc) callVoidPtrFunc(free, sourceNameAlloc);
      callVoidPtrFunc(free, sourceAlloc);
    }
  }

  function prepareCompile(input, options, plainExport, namedExport, virtualExport) {
    const normalized = normalizeCompileInput(input);
    const resourceLimits = resolveResourceLimits(
      compilerResourceLimits,
      options?.limits ?? {},
    );
    const sourceByteLength = utf8ByteLength(normalized.source);
    assertResourceLimit("maxSourceBytes", sourceByteLength, resourceLimits.maxSourceBytes);
    assertResourceLimit(
      "maxTotalSourceBytes", sourceByteLength, resourceLimits.maxTotalSourceBytes,
    );
    const sourceBytes = encoder.encode(`${normalized.source}\0`);
    const sourceNameBytes = normalized.name === null
      ? null
      : encoder.encode(`${normalized.name}\0`);
    const virtualHeaders = prepareVirtualHeaders(options, encoder, resourceLimits);
    if (virtualHeaders) {
      if (typeof virtualExport !== "function") {
        throw new Error("ag_c wasm module does not support virtual headers");
      }
      return {
        sourceBytes, sourceNameBytes, virtualHeaders,
        compileFn: virtualExport, resourceLimits,
      };
    }
    if (sourceNameBytes && typeof namedExport !== "function") {
      throw new Error("ag_c wasm module does not support named sources");
    }
    return {
      sourceBytes,
      sourceNameBytes,
      virtualHeaders: null,
      compileFn: sourceNameBytes ? namedExport : plainExport,
      resourceLimits,
    };
  }

  function compileWat(source, options = {}) {
    const prepared = prepareCompile(
      source, options, compileWatExport, compileWatNamedExport, compileWatVirtualExport,
    );
    if (useHeapBuffers) {
      return compileWithHeapBuffers(
        prepared.sourceBytes, prepared.sourceNameBytes, prepared.virtualHeaders,
        prepared.compileFn, true, prepared.resourceLimits,
        DEFAULT_WAT_OUTPUT_LIMIT, null,
      );
    }
    return compileWithFixedBuffers(
      prepared.sourceBytes, prepared.sourceNameBytes, prepared.virtualHeaders,
      prepared.compileFn, true, prepared.resourceLimits,
      DEFAULT_WAT_OUTPUT_LIMIT, null,
    );
  }

  function compileObject(source, options = {}) {
    if (typeof compileObjectExport !== "function") {
      throw new Error("ag_c wasm module does not export agc_wasm_compile_object");
    }
    const resetContinuation = configureContinuation(options.continuation);
    try {
      const prepared = prepareCompile(
        source, options, compileObjectExport, compileObjectNamedExport, compileObjectVirtualExport,
      );
      if (useHeapBuffers) {
        return compileWithHeapBuffers(
          prepared.sourceBytes, prepared.sourceNameBytes, prepared.virtualHeaders,
          prepared.compileFn, false, prepared.resourceLimits,
          prepared.resourceLimits.maxObjectBytes, "maxObjectBytes",
        );
      }
      return compileWithFixedBuffers(
        prepared.sourceBytes, prepared.sourceNameBytes, prepared.virtualHeaders,
        prepared.compileFn, false, prepared.resourceLimits,
        prepared.resourceLimits.maxObjectBytes, "maxObjectBytes",
      );
    } finally {
      resetContinuation();
    }
  }

  function compileWatWithDiagnostics(source, options = {}) {
    const wat = compileWat(source, options);
    return { wat, diagnostics: readStructuredDiagnostics() };
  }

  function compileObjectWithDiagnostics(source, options = {}) {
    const object = compileObject(source, options);
    return { object, diagnostics: readStructuredDiagnostics() };
  }

  return {
    instance,
    memory,
    compileWat,
    compileWatWithDiagnostics,
    compileObject,
    compileObjectWithDiagnostics,
    readStdout,
    readStderr: readStderrText,
    readDiagnostics: readStructuredDiagnostics,
    readTermination,
    diagnosticCoordinateSystem: Object.freeze({
      encoding: "utf-8",
      input: "normalized",
      offsetBase: 0,
      lineBase: 1,
      columnBase: 1,
      end: "exclusive",
    }),
    resourceLimits: compilerResourceLimits,
    limits: { sourcePtr, sourceCap, outputPtr, outputCap, initialOutputCap, useHeapBuffers },
  };
}

export default createCompiler;
