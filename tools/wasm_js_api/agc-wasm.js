const DEFAULT_SOURCE_PTR = 393216;
const DEFAULT_SOURCE_CAP = 32768;
const DEFAULT_OUTPUT_PTR = DEFAULT_SOURCE_PTR + DEFAULT_SOURCE_CAP;
const DEFAULT_OUTPUT_CAP = 98304;
const DEFAULT_INITIAL_OUTPUT_CAP = 131072;

function asBytes(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError("wasm must be an ArrayBuffer or Uint8Array");
}

async function instantiateFromSource(wasmSource, imports = {}) {
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

function callCompile(fn, sourcePtr, outputPtr, outputCap) {
  try {
    return Number(fn(BigInt(sourcePtr), BigInt(outputPtr), BigInt(outputCap)));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(fn(sourcePtr, outputPtr, outputCap));
    }
    throw err;
  }
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
        stdoutChunks.push(text);
        if (typeof options.onStdout === "function") options.onStdout(text);
    },
    __agc_runtime_stderr_write(ptr, len) {
      const text = readCallbackBytes(ptr, len);
      if (!text) return;
      stderrChunks.push(text);
      if (typeof options.onStderr === "function") options.onStderr(text);
    },
  };
  const imports = { env: envImports };
  const instance = await instantiateFromSource(wasmSource, imports);
  const memory = instance.exports.memory;
  callbackMemory = memory;
  const compileWatExport = instance.exports.agc_wasm_compile_wat;
  const compileObjectExport = instance.exports.agc_wasm_compile_object;
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

  const sourcePtr = options.sourcePtr ?? DEFAULT_SOURCE_PTR;
  const sourceCap = options.sourceCap ?? DEFAULT_SOURCE_CAP;
  const outputPtr = options.outputPtr ?? DEFAULT_OUTPUT_PTR;
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
  const encoder = new TextEncoder();

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

  function readDiagnostics() {
    return (stderrChunks.join("") || readFallbackBuffer(stderrPtrExport, stderrLenExport)).trim();
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

  function throwCompileFailure(errOrCode, outputCap) {
    notifyTermination();
    const diag = readDiagnostics();
    if (diag) throw new Error(diag);
    const termination = readTermination();
    if (termination) {
      if (termination.kind === "exit") throw new Error(`ag_c wasm exited with status ${termination.status}`);
      if (termination.kind === "abort") throw new Error("ag_c wasm aborted");
    }
    if (typeof errOrCode === "number") throw new Error(compileErrorMessage(errOrCode, outputCap));
    throw errOrCode;
  }

  function compileWithFixedBuffers(sourceBytes, compileFn, asText) {
    if (sourceBytes.length > sourceCap) {
      throw new RangeError(`source is ${sourceBytes.length} bytes; max is ${sourceCap}`);
    }
    if (sourcePtr + sourceCap > memory.buffer.byteLength) {
      throw new RangeError("configured source buffer is outside wasm memory");
    }
    if (outputPtr + outputCap > memory.buffer.byteLength) {
      throw new RangeError("configured output buffer is outside wasm memory");
    }

    let mem = new Uint8Array(memory.buffer);
    mem.fill(0, sourcePtr, sourcePtr + sourceCap);
    mem.fill(0, outputPtr, outputPtr + outputCap);
    mem.set(sourceBytes, sourcePtr);

    resetDiagnostics();
    let n;
    try {
      n = callCompile(compileFn, sourcePtr, outputPtr, outputCap);
    } catch (err) {
      throwCompileFailure(err, outputCap);
    }
    if (n < 0) throwCompileFailure(n, outputCap);

    mem = new Uint8Array(memory.buffer);
    const bytes = mem.slice(outputPtr, outputPtr + n);
    return asText ? decoder.decode(bytes) : bytes;
  }

  function compileWithHeapBuffers(sourceBytes, compileFn, asText) {
    const sourceAlloc = callPtrFunc(malloc, sourceBytes.length);
    if (!sourceAlloc) throw new Error("ag_c wasm malloc failed for source buffer");
    let outputAlloc = 0;
    let cap = initialOutputCap;
    try {
      let mem = new Uint8Array(memory.buffer);
      ensureMemoryRange(memory, sourceAlloc, sourceBytes.length, "source");
      mem.set(sourceBytes, sourceAlloc);

      for (;;) {
        outputAlloc = callPtrFunc(malloc, cap);
        if (!outputAlloc) throw new Error("ag_c wasm malloc failed for output buffer");
        mem = new Uint8Array(memory.buffer);
        ensureMemoryRange(memory, outputAlloc, cap, "output");
        mem.fill(0, outputAlloc, outputAlloc + cap);
        resetDiagnostics();
        let n;
        try {
          n = callCompile(compileFn, sourceAlloc, outputAlloc, cap);
        } catch (err) {
          throwCompileFailure(err, cap);
        }
        if (n === -2) {
          callVoidPtrFunc(free, outputAlloc);
          outputAlloc = 0;
          cap *= 2;
          if (cap > 16 * 1024 * 1024) {
            throw new RangeError("ag_c wasm output exceeded 16 MiB");
          }
          continue;
        }
        if (n < 0) throwCompileFailure(n, cap);
        mem = new Uint8Array(memory.buffer);
        const bytes = mem.slice(outputAlloc, outputAlloc + n);
        return asText ? decoder.decode(bytes) : bytes;
      }
    } finally {
      if (outputAlloc) callVoidPtrFunc(free, outputAlloc);
      callVoidPtrFunc(free, sourceAlloc);
    }
  }

  function compileWat(source) {
    const sourceBytes = encoder.encode(`${source}\0`);
    if (useHeapBuffers) return compileWithHeapBuffers(sourceBytes, compileWatExport, true);
    return compileWithFixedBuffers(sourceBytes, compileWatExport, true);
  }

  function compileObject(source) {
    if (typeof compileObjectExport !== "function") {
      throw new Error("ag_c wasm module does not export agc_wasm_compile_object");
    }
    const sourceBytes = encoder.encode(`${source}\0`);
    if (useHeapBuffers) return compileWithHeapBuffers(sourceBytes, compileObjectExport, false);
    return compileWithFixedBuffers(sourceBytes, compileObjectExport, false);
  }

  return {
    instance,
    memory,
    compileWat,
    compileObject,
    readStdout,
    readStderr: readDiagnostics,
    readTermination,
    limits: { sourcePtr, sourceCap, outputPtr, outputCap, initialOutputCap, useHeapBuffers },
  };
}

export default createCompiler;
