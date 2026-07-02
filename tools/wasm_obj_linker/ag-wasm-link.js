import { createAgcRuntimeImports } from "../wasm_js_api/agc-runtime-imports.js";

function asBytes(input, label) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError(`${label} must be an ArrayBuffer or Uint8Array`);
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
  return (await WebAssembly.instantiate(asBytes(wasmSource, "wasm"), imports)).instance;
}

function callMalloc(malloc, size) {
  return Number(malloc(BigInt(size || 1)));
}

function callFree(free, ptr) {
  free(ptr);
}

export async function createLinker(wasmSource, options = {}) {
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
  const imports = createAgcRuntimeImports({ env: envImports });
  const instance = await instantiateFromSource(wasmSource, imports);
  const memory = instance.exports.memory;
  callbackMemory = memory;
  const malloc = instance.exports.malloc;
  const free = instance.exports.free;
  const linkObjects = instance.exports.agc_wasm_link_objects;
  const stdoutPtrExport = instance.exports.__agc_runtime_stdout_ptr;
  const stdoutLenExport = instance.exports.__agc_runtime_stdout_len;
  const stderrPtrExport = instance.exports.__agc_runtime_stderr_ptr;
  const stderrLenExport = instance.exports.__agc_runtime_stderr_len;
  const stderrResetExport = instance.exports.__agc_runtime_stderr_reset;
  const terminationKindExport = instance.exports.__agc_runtime_termination_kind;
  const terminationStatusExport = instance.exports.__agc_runtime_termination_status;
  if (!(memory instanceof WebAssembly.Memory)) {
    throw new Error("ag_wasm_link wasm module does not export memory");
  }
  if (typeof malloc !== "function" || typeof free !== "function") {
    throw new Error("ag_wasm_link wasm module must export malloc/free");
  }
  if (typeof linkObjects !== "function") {
    throw new Error("ag_wasm_link wasm module does not export agc_wasm_link_objects");
  }

  let u8 = new Uint8Array(memory.buffer);
  let view = new DataView(memory.buffer);
  function refresh() {
    u8 = new Uint8Array(memory.buffer);
    view = new DataView(memory.buffer);
  }
  function setU64(addr, value) {
    view.setBigUint64(addr, BigInt(value), true);
  }
  function getU64(addr) {
    return Number(view.getBigUint64(addr, true));
  }
  function allocBytes(bytes) {
    const ptr = callMalloc(malloc, bytes.length || 1);
    if (!ptr) throw new Error("ag_wasm_link malloc failed");
    refresh();
    u8.set(bytes, ptr);
    return ptr;
  }
  function allocCString(s) {
    return allocBytes(new TextEncoder().encode(`${s}\0`));
  }

  function callNoArgNumberFunc(fn) {
    return Number(fn());
  }

  function callNoArgVoidFunc(fn) {
    fn();
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

  function readStderr() {
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

  function throwLinkFailure(err) {
    notifyTermination();
    const diag = readStderr();
    const termination = readTermination();
    if (diag) throw new Error(diag);
    if (termination && termination.kind === "exit") {
      throw new Error(`ag_wasm_link exited with status ${termination.status}`);
    }
    if (termination && termination.kind === "abort") throw new Error("ag_wasm_link aborted");
    if (err) throw err;
    throw new Error("ag_wasm_link failed");
  }

  function link(objects, options = {}) {
    resetDiagnostics();
    if (!Array.isArray(objects) || objects.length === 0) {
      throw new TypeError("objects must be a non-empty array");
    }
    const exportNames = options.exports ?? ["main"];
    if (!Array.isArray(exportNames)) throw new TypeError("exports must be an array");
    const useStdlib = options.useStdlib ?? true;
    const allocations = [];
    let linkedPtr = 0;
    try {
      const objectBytes = objects.map((obj, i) => asBytes(obj, `objects[${i}]`));
      const descPtr = callMalloc(malloc, objectBytes.length * 16);
      allocations.push(descPtr);
      refresh();
      for (let i = 0; i < objectBytes.length; i++) {
        const ptr = allocBytes(objectBytes[i]);
        allocations.push(ptr);
        setU64(descPtr + i * 16, ptr);
        setU64(descPtr + i * 16 + 8, objectBytes[i].length);
      }

      const exportPtrs = exportNames.map((name) => {
        if (typeof name !== "string" || name.length === 0) {
          throw new TypeError("export names must be non-empty strings");
        }
        const ptr = allocCString(name);
        allocations.push(ptr);
        return ptr;
      });
      const exportsPtr = callMalloc(malloc, Math.max(1, exportPtrs.length) * 8);
      allocations.push(exportsPtr);
      refresh();
      for (let i = 0; i < exportPtrs.length; i++) setU64(exportsPtr + i * 8, exportPtrs[i]);

      const outLenPtr = callMalloc(malloc, 8);
      allocations.push(outLenPtr);
      refresh();
      setU64(outLenPtr, 0);

      try {
        linkedPtr = Number(linkObjects(
          BigInt(descPtr),
          BigInt(objectBytes.length),
          BigInt(exportsPtr),
          BigInt(exportPtrs.length),
          BigInt(useStdlib ? 1 : 0),
          BigInt(outLenPtr),
        ));
      } catch (err) {
        throwLinkFailure(err);
      }
      refresh();
      const linkedLen = getU64(outLenPtr);
      if (!linkedPtr || linkedLen < 8) {
        throwLinkFailure();
      }
      return new Uint8Array(u8.slice(linkedPtr, linkedPtr + linkedLen));
    } finally {
      if (linkedPtr) callFree(free, linkedPtr);
      for (let i = allocations.length - 1; i >= 0; i--) callFree(free, allocations[i]);
    }
  }

  return {
    instance,
    memory,
    link,
    readStdout,
    readStderr,
    readTermination,
  };
}

export default createLinker;
